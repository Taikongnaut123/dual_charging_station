#include "stationcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <cmath>
#include <cstdio>

namespace
{

    constexpr int kCurveMaxPoints = 180; // 曲线保留点数（2Hz x 180 = 90 秒窗口）
    constexpr int kCurveKeepEvery = 1;   // 每帧都采（设备本身就是 2Hz）

    // ---- 剩余时间估算（CC/CV 两段模型）----
    // CV 段电流衰减时间常数（小时）。规格书没给充电曲线，这是 21700 三元
    // 电芯在 0.5C 下的经验值：τ=0.25h 时恒压段约 48 分钟、充入约 12% 电量。
    constexpr double kCvTauHours = 0.25;
    // 电流回落到 CC 值的这个比例以下，就认为进入恒压段
    constexpr double kCvDetectRatio = 0.85;
    // 剩余时间指数平滑系数（2Hz 下约 2.5 秒时间常数）
    constexpr double kRemainSmoothAlpha = 0.2;
    // 电池组最低可信电压（V）。13S 电池组放到 2.5V/串 也有 32.5V，
    // 低于这个值只可能是没接电池或采样异常。
    constexpr double kMinPackVoltageV = 30.0;

    /**
     * 统计文件的绝对路径（累计充电次数 / 累计电量）。
     *
     * 不能直接用 QSettings(组织名, 程序名)：那条路依赖 $XDG_CONFIG_HOME 或
     * $HOME 算目录，而**由 systemd 拉起的 system service 这两者都没设**。
     * 拿不到路径时 QSettings 不会报错，只是读回来全是默认值、写下去直接丢掉，
     * 现场表现就是“累计次数/电量永远是 0，重启一次清一次”。
     *
     * 所以自己算：优先走标准位置（手工跑的时候跟以前完全一致），两个环境变量
     * 都没有时落到**可执行文件旁边的 config 目录**，跟桥把日志收到包目录的做法一致：
     * 包目录挪到哪，统计就跟到哪，不依赖谁的环境变量。
     */
    QString statisticsFilePath()
    {
        static const QString path = []
        {
            QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
            if (base.isEmpty())
            {
                const QString home = qEnvironmentVariable("HOME");
                if (!home.isEmpty())
                    base = home + QLatin1String("/.config");
            }
            if (base.isEmpty())
            {
                base = QCoreApplication::applicationDirPath() + QLatin1String("/../config");
                std::fprintf(stderr,
                             "[统计] 没有 XDG_CONFIG_HOME / HOME，累计统计改写到 %s\n",
                             qPrintable(QFileInfo(base).absoluteFilePath()));
            }
            const QString dir = base + QLatin1String("/RobotCharge");
            return dir + QLatin1String("/DualChargingStation.conf");
        }();
        return path;
    }

} // namespace

StationController::StationController(BridgeClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    connect(m_client, &BridgeClient::dataReceived, this, &StationController::onData);
    connect(m_client, &BridgeClient::linkStateChanged,
            this, &StationController::onLinkStateChanged);

    m_clock.start();
    m_lastIntegrateMs = m_clock.elapsed();
    loadStatistics();
}

StationController::~StationController()
{
    saveStatistics();
}

QStringList StationController::ccuStageLabels()
{
    return QStringList{QStringLiteral("初始化中"), QStringLiteral("初始化完成"),
                       QStringLiteral("回零中"), QStringLiteral("回零完成"),
                       QStringLiteral("到位中"), QStringLiteral("到位完成")};
}

QString StationController::stateText(State s)
{
    switch (s)
    {
    case State::Offline:
        return QStringLiteral("无数据");
    case State::Fault:
        return QStringLiteral("充电桩故障");
    case State::Idle:
        return QStringLiteral("空闲");
    case State::Charging:
        return QStringLiteral("充电中");
    case State::Full:
        return QStringLiteral("已充满");
    }
    return QStringLiteral("未知");
}

QString StationController::formatDuration(int seconds)
{
    if (seconds < 0)
        return QStringLiteral("--");
    const int h = seconds / 3600;
    const int m = (seconds % 3600) / 60;
    const int s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1 时 %2 分").arg(h).arg(m);
    if (m > 0)
        return QStringLiteral("%1 分 %2 秒").arg(m).arg(s);
    return QStringLiteral("%1 秒").arg(s);
}

int StationController::activePort() const
{
    if (m_data.manualCharging() || m_data.manualLinked())
        return 1;
    if (m_data.autoCharging() || m_data.autoLinked())
        return 2;

    // 都还没连上时，用充电模式兜底
    if (m_data.chargeMode == 1)
        return 1;
    if (m_data.chargeMode == 2)
        return 2;
    return 0;
}

// ------------------------------------------------------------------ 数据入口

void StationController::onData(const MdcData &d)
{
    m_data = d;
    m_hasData = true;

    evaluateState(d);
    updateCcuStage(d);
    integrate(d);

    emit dataChanged(m_data);
}

QString StationController::offlineDetail() const
{
    switch (m_offlineReason)
    {
    case OfflineReason::NoBridge:
        return QStringLiteral("收不到 mdc_bridge 的心跳：桥进程没运行，或网络/端口不通");
    case OfflineReason::NoDevice:
        return QStringLiteral("桥在线，但设备没有上报数据：检查串口连接与 mdc_node 是否在跑");
    case OfflineReason::None:
        break;
    }
    return QString();
}

void StationController::onLinkStateChanged(BridgeClient::LinkState s)
{
    if (s == BridgeClient::LinkState::Online)
    {
        m_offlineReason = OfflineReason::None;
        return;
    }

    // 注意 BridgeOnly 也要处理：桥活着但设备不上报时，界面上的数值已经过期，
    // 继续亮着"充电中"和冻结的读数会误导操作员（而且环形表的动画还在转）。
    // 这里统一归为"无数据"，具体原因由 offlineReason() 区分，页眉胶囊也会显示。
    m_offlineReason = (s == BridgeClient::LinkState::NoBridge) ? OfflineReason::NoBridge
                                                               : OfflineReason::NoDevice;
    m_hasData = false;

    if (m_state != State::Offline)
    {
        m_state = State::Offline;
        // 掉线时把本次充电会话结算掉，避免统计悬空
        if (m_sessionSeconds > 0)
            finishSession();
        emit stateChanged(m_state);
    }
}

// ------------------------------------------------------------------ 状态派生

void StationController::evaluateState(const MdcData &d)
{
    const State before = m_state;

    State next;
    if (!m_hasData)
    {
        next = State::Offline;
    }
    else
    {
        switch (d.chargeState)
        {
        case 0:
            next = State::Fault;
            break;
        case 2:
            next = State::Charging;
            break;
        case 3:
            next = State::Full;
            break;
        case 1:
        default:
            next = State::Idle;
            break;
        }
    }

    // 会话开始/结束判定
    if (before != State::Charging && next == State::Charging)
        startSession();
    else if (before == State::Charging && next != State::Charging)
        finishSession();

    if (before != next)
    {
        m_state = next;
        emit stateChanged(m_state);
    }
}

void StationController::updateCcuStage(const MdcData &d)
{
    const int beforeCurrent = m_ccuStageCurrent;
    const int beforeReached = m_ccuStageReached;
    const bool beforeFaulted = m_ccuFaulted;

    m_ccuFaulted = d.ccuFault();

    if (!m_hasData)
    {
        m_ccuStageCurrent = -1;
        m_ccuStageReached = -1;
    }
    else if (m_ccuFaulted)
    {
        m_ccuStageCurrent = -2; // 断联/错误：保留已到达的进度但标红
    }
    else if (d.ccuMotorStatus >= 1 && d.ccuMotorStatus <= 6)
    {
        m_ccuStageCurrent = d.ccuMotorStatus - 1;
        m_ccuStageReached = qMax(m_ccuStageReached, m_ccuStageCurrent);
    }
    else
    {
        m_ccuStageCurrent = -1; // 0 或未知值：不显示进度
    }

    if (beforeCurrent != m_ccuStageCurrent || beforeReached != m_ccuStageReached ||
        beforeFaulted != m_ccuFaulted)
        emit ccuStageChanged();
}

// ------------------------------------------------------------------ 本地统计

void StationController::startSession()
{
    m_sessionPort = activePort();
    m_sessionEnergyKwh = 0.0;
    m_sessionSeconds = 0;
    m_sessionSecondsAcc = 0.0;
    m_sessionStartSoc = (m_sessionPort == 2) ? m_data.autoSoc : m_data.manualSoc;
    m_curve.clear();
    m_curveDivider = 0;
    m_lastIntegrateMs = m_clock.elapsed();

    // 新会话：清掉上一次的剩余时间估算与 CC 电流观测值
    m_remainingSeconds = -1;
    m_remainingRaw = -1.0;
    m_ccCurrentA = 0.0;
}

void StationController::finishSession()
{
    if (m_sessionSeconds <= 0 && m_sessionEnergyKwh <= 0.0)
        return;

    m_totalSessions += 1;
    m_totalEnergyKwh += m_sessionEnergyKwh;
    saveStatistics();
    emit statisticsChanged();

    // 注意：这里**不**清零 m_sessionEnergyKwh / m_sessionSeconds。
    // 充完电后操作员还要能看到本次结果，直到下一次充电开始才重置。
}

void StationController::integrate(const MdcData &d)
{
    const qint64 now = m_clock.elapsed();
    double dt = (now - m_lastIntegrateMs) / 1000.0;
    m_lastIntegrateMs = now;
    if (dt <= 0.0 || dt > 5.0)
        dt = 0.5; // 异常间隔（首次/卡顿）按一帧处理

    if (d.charging())
    {
        // 环回模式把 40 秒仿真成一个完整充电周期，时间指标需要按同一比例缩放，
        // 否则"已充时长"只有十几秒、而"预计剩余"算出来是几十分钟，自相矛盾。
        const double timeScale = m_client->isLoopbackEnabled() ? 60.0 : 1.0;
        m_sessionSecondsAcc += dt * timeScale;
        m_sessionSeconds = int(m_sessionSecondsAcc);

        // 充电量用 SOC 增量×额定容量估算：不依赖墙钟时间，
        // 与显示的 SOC、剩余时间用的是同一套容量模型，三者自洽。
        const double soc = (m_sessionPort == 2) ? d.autoSoc : d.manualSoc;
        const double delta = (soc - m_sessionStartSoc) / 100.0 * packCapacityKwh();
        m_sessionEnergyKwh = qMax(0.0, delta);
    }

    // 剩余时间每帧重算（2Hz），内部做指数平滑，界面直接读缓存值
    updateRemainingEstimate(d);

    // 曲线：两个口的充电电流（A）
    if (++m_curveDivider >= kCurveKeepEvery)
    {
        m_curveDivider = 0;
        CurvePoint p;
        p.time = QDateTime::currentDateTime();
        p.manualA = d.manualChargeCurrentA();
        p.autoA = d.autoChargeCurrentA();
        m_curve.append(p);
        while (m_curve.size() > kCurveMaxPoints)
            m_curve.remove(0);
    }
}

/**
 * 按 CC/CV 两段模型估算剩余充电时间。
 *
 * 依据 CDRX4810 规格书 V0.2（13S2P / 10Ah / 46.8V）：
 *   · 充电模式      CC/CV 恒流恒压
 *   · 充电限制电压  54.6V（4.2V × 13）
 *   · 充电截止电流  0.02C = 200mA
 *   · 标准充电电流  0.5C = 5A，最大 1.0C = 10A
 *
 * 为什么不能直接用「剩余电量 ÷ 当前功率」：
 * 锂电充到最后一段是恒压，电流会一路衰减到 200mA。按当前功率线性外推，
 * 电流一掉剩余时间就暴涨，最后半小时能算出好几个小时，读数完全没法看。
 *
 * 两段模型：
 *   CC 段：电流恒定 I_cc，充到 SOC 达到 CV 转折点 soc_cv
 *   CV 段：电压恒定，电流按指数衰减 I(t) = I_cc · e^(-t/τ)，
 *          衰减到截止电流 I_taper 时结束
 *
 * CV 段耗时      t_cv = τ · ln(I_cc / I_taper)
 * CV 段充入电量  Q_cv = I_cc · τ · (1 - I_taper / I_cc)
 * CV 转折 SOC    soc_cv = 100 - Q_cv / C · 100
 *
 * 这样 soc_cv 是由 τ 和 I_cc 推出来的，不是另外拍一个数，模型内部自洽。
 * 代入 5A / 0.2A / τ=0.25h：t_cv ≈ 48 分钟，Q_cv ≈ 1.2Ah（12%），
 * soc_cv ≈ 88% —— 与锂电「恒压段约占一成多电量、耗时半小时到一小时」吻合。
 *
 * τ 是唯一没法从规格书直接读到的量（规格书只给容量、内阻、截止电流，
 * 没有充电曲线）。0.25h 是这类 21700 三元电芯在 0.5C 下的经验值，
 * 现场如果发现恒压段明显偏长/偏短，改 kCvTauHours 即可。
 *
 * 内阻 R≤4mΩ/电芯（13S2P 约 26mΩ）没有参与计算：设备直接上报 SOC，
 * 不需要用电压反推 SOC；而 5A 下 IR 压降只有 0.13V，对 CV 判定影响可忽略。
 * 判定 CV 用的是「电流是否已从 CC 值明显回落」，比看电压可靠。
 */
void StationController::updateRemainingEstimate(const MdcData &d)
{
    const int port = activePort();
    if (!d.charging() || port == 0)
    {
        m_remainingSeconds = -1;
        m_remainingRaw = -1.0;
        return;
    }

    const bool autoPort = (port == 2);
    const double soc = autoPort ? d.autoSoc : d.manualSoc;
    const double currentA = autoPort ? d.autoChargeCurrentA() : d.manualChargeCurrentA();
    const double voltageV = autoPort ? d.autoVoltageV() : d.manualVoltageV();

    // 电流太小（没真正在充 / 刚上电还没起流）时不给估算，免得显示一个巨大的数。
    //
    // 电压也要校验：现场见过「充电开关打开但电池没接」的状态，设备照样报
    // 2~3A 电流、SOC 报 0%、电压报 0.00V。只看电流的话会把它当成正常充电，
    // 按 SOC=0% 算出「还要 4 小时」，屏幕上挂着一个毫无意义的读数。
    if (currentA < 0.1 || voltageV < kMinPackVoltageV || m_packCapacityAh <= 0.0)
    {
        m_remainingSeconds = -1;
        m_remainingRaw = -1.0;
        return;
    }

    // CC 段电流：取本次会话观测到的最大值。
    // 刚插上时电流还在爬升，用瞬时值会把 CC 电流低估，剩余时间算长。
    m_ccCurrentA = qMax(m_ccCurrentA, currentA);
    const double iCc = qMax(m_ccCurrentA, currentA);
    const double iTaper = qMax(m_taperCurrentA, 0.01);

    // CV 段参数（见上方推导）
    const double tauH = kCvTauHours;
    const double cvHours = tauH * std::log(iCc / iTaper);
    const double cvChargeAh = iCc * tauH * (1.0 - iTaper / iCc);
    // 转折点 SOC 下限取 70%：cvChargeAh 随 iCc 线性增长，电流报得偏大时
    // （比如环回仿真的 15A，已超规格书 10A 上限）会把转折点压到 60% 出头，
    // 明显不合常理。恒压段充入电量实际很少超过 30%，这里兜一下底。
    const double socCv = qBound(70.0, 100.0 - cvChargeAh / m_packCapacityAh * 100.0, 99.0);

    // 判定当前在 CC 还是 CV：电流已从 CC 值明显回落、且电压接近限制值。
    // 另外 SOC 一旦越过转折点 socCv，即使电流还没明显回落，也已经进入恒压段了
    // （否则 SOC=100% 时还会算出「还要 48 分钟」，明显不对）。
    const bool inCv = (currentA < iCc * kCvDetectRatio) &&
                      (voltageV > m_chargeLimitV - 1.0);

    double seconds = 0.0;
    if (soc >= 99.5)
    {
        // 已经报满，直接归零
        seconds = 0.0;
    }
    else if (inCv || soc >= socCv)
    {
        // 恒压段：从当前电流衰减到截止电流还要多久。
        // 电流仍等于 CC 值时，这个式子正好退化成整个 CV 段时长 cvHours。
        const double ratio = qMax(currentA, iTaper) / iTaper;
        seconds = tauH * std::log(ratio) * 3600.0;
    }
    else
    {
        // 恒流段：先充到 CV 转折点，再加上整个 CV 段
        const double ccAh = qMax(0.0, socCv - soc) / 100.0 * m_packCapacityAh;
        seconds = ccAh / iCc * 3600.0 + cvHours * 3600.0;
    }

    // 环回模式把 40 秒仿真成一个完整充电周期，剩余时间要按同一比例压缩，
    // 否则「已充时长」十几秒、「预计剩余」几十分钟，自相矛盾。
    if (m_client->isLoopbackEnabled())
        seconds /= 60.0;

    // 指数平滑：设备 2Hz 上报，电流本身有抖动，不平滑的话读数每 0.5 秒跳一次。
    // α=0.2 对应约 2.5 秒的时间常数，既能压住抖动，又跟得上真实变化。
    if (m_remainingRaw < 0.0)
        m_remainingRaw = seconds;
    else
        m_remainingRaw += kRemainSmoothAlpha * (seconds - m_remainingRaw);

    m_remainingSeconds = int(m_remainingRaw + 0.5);
}

void StationController::loadStatistics()
{
    // 显式指定 IniFormat + 绝对路径：不依赖环境变量，行为可预测
    QSettings settings(statisticsFilePath(), QSettings::IniFormat);
    m_totalSessions = settings.value(QStringLiteral("stats/sessions"), 0).toInt();
    m_totalEnergyKwh = settings.value(QStringLiteral("stats/totalEnergyKwh"), 0.0).toDouble();

    // 电池组容量：默认 10Ah（CDRX4810，13S2P）。
    // 兼容旧配置键 config/packCapacityKwh（kWh）：老版本写的是 1.92，
    // 那是 48V/40Ah 的假设值，与现场电池不符，读到就按 Ah 换算过来。
    if (settings.contains(QStringLiteral("config/packCapacityAh")))
    {
        m_packCapacityAh = settings.value(QStringLiteral("config/packCapacityAh"), 10.0).toDouble();
    }
    else if (settings.contains(QStringLiteral("config/packCapacityKwh")))
    {
        const double kwh = settings.value(QStringLiteral("config/packCapacityKwh"), 0.0).toDouble();
        if (kwh > 0.0)
            m_packCapacityAh = kwh * 1000.0 / m_packNominalV;
    }
    if (m_packCapacityAh <= 0.0)
        m_packCapacityAh = 10.0;
}

void StationController::saveStatistics()
{
    const QString path = statisticsFilePath();
    // QSettings 会建文件，但不会建中间的目录
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("stats/sessions"), m_totalSessions);
    settings.setValue(QStringLiteral("stats/totalEnergyKwh"), m_totalEnergyKwh);
    settings.sync();
}

void StationController::resetStatistics()
{
    m_totalSessions = 0;
    m_totalEnergyKwh = 0.0;
    saveStatistics();
    emit statisticsChanged();
}
