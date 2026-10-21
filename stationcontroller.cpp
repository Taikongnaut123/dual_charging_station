#include "stationcontroller.h"

#include <QSettings>

namespace
{

    constexpr int kCurveMaxPoints = 180; // 曲线保留点数（2Hz x 180 = 90 秒窗口）
    constexpr int kCurveKeepEvery = 1;   // 每帧都采（设备本身就是 2Hz）

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

void StationController::onLinkStateChanged(BridgeClient::LinkState s)
{
    if (s == BridgeClient::LinkState::NoBridge)
    {
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
        const double delta = (soc - m_sessionStartSoc) / 100.0 * m_packCapacityKwh;
        m_sessionEnergyKwh = qMax(0.0, delta);
    }

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

int StationController::remainingSeconds() const
{
    const int port = activePort();
    if (!m_data.charging() || port == 0)
        return -1;

    const double soc = (port == 2) ? m_data.autoSoc : m_data.manualSoc;
    const double power = (port == 2) ? m_data.autoChargePowerKw() : m_data.manualChargePowerKw();
    if (power < 0.05)
        return -1;

    const double need = (100.0 - soc) / 100.0 * m_packCapacityKwh;
    return int(need / power * 3600.0);
}

void StationController::loadStatistics()
{
    QSettings settings(QStringLiteral("RobotCharge"), QStringLiteral("DualChargingStation"));
    m_totalSessions = settings.value(QStringLiteral("stats/sessions"), 0).toInt();
    m_totalEnergyKwh = settings.value(QStringLiteral("stats/totalEnergyKwh"), 0.0).toDouble();
    m_packCapacityKwh = settings.value(QStringLiteral("config/packCapacityKwh"), 1.92).toDouble();
}

void StationController::saveStatistics()
{
    QSettings settings(QStringLiteral("RobotCharge"), QStringLiteral("DualChargingStation"));
    settings.setValue(QStringLiteral("stats/sessions"), m_totalSessions);
    settings.setValue(QStringLiteral("stats/totalEnergyKwh"), m_totalEnergyKwh);
}

void StationController::resetStatistics()
{
    m_totalSessions = 0;
    m_totalEnergyKwh = 0.0;
    saveStatistics();
    emit statisticsChanged();
}
