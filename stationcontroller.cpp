#include "stationcontroller.h"

#include <QSettings>

namespace
{

    constexpr int kCurveMaxPoints = 180; // 曲线保留点数
    constexpr int kCurveKeepEvery = 2;   // 每 N 帧采一个点（10Hz / 2 = 5Hz）

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

QStringList StationController::stageLabels()
{
    return QStringList{QStringLiteral("停车检测"), QStringLiteral("停车握手"),
                       QStringLiteral("机器人落座"), QStringLiteral("充电中"),
                       QStringLiteral("充电完成")};
}

QString StationController::stateText(State s)
{
    switch (s)
    {
    case State::Offline:
        return QStringLiteral("无数据");
    case State::Waiting:
        return QStringLiteral("等待机器人");
    case State::Detecting:
        return QStringLiteral("停车检测中");
    case State::Handshaked:
        return QStringLiteral("握手成功");
    case State::Ready:
        return QStringLiteral("已就位");
    case State::Charging:
        return QStringLiteral("充电中");
    case State::Completed:
        return QStringLiteral("充电完成");
    case State::Alarm:
        return QStringLiteral("报警");
    case State::Emergency:
        return QStringLiteral("紧急停止");
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

// ------------------------------------------------------------------ 数据入口

void StationController::onData(const MdcData &d)
{
    m_data = d;
    m_hasData = true;

    evaluateState(d);
    updateStage(d);
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

    State next = m_state;
    m_stageFaulted = false;

    if (!m_hasData)
    {
        next = State::Offline;
    }
    else if (d.emergency())
    {
        next = State::Emergency;
        m_stageFaulted = true;
    }
    else if (d.alarm())
    {
        next = State::Alarm;
        m_stageFaulted = true;
    }
    else if (d.charging())
    {
        next = State::Charging;
        m_hadChargeThisVisit = true;
    }
    else if (m_hadChargeThisVisit && d.seated())
    {
        next = State::Completed; // 充完电但机器人还没走
    }
    else if (d.seated())
    {
        next = State::Ready;
    }
    else if (d.handshaked())
    {
        next = State::Handshaked;
    }
    else if (d.anyIrBlocked())
    {
        next = State::Detecting;
    }
    else
    {
        next = State::Waiting;
        m_hadChargeThisVisit = false; // 离开充电位，拜访结束
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

void StationController::updateStage(const MdcData &d)
{
    int current = -1;
    if (!m_hasData)
    {
        current = -1;
    }
    else if (m_stageFaulted)
    {
        current = -2; // 故障：保留已到达的进度但标红
    }
    else if (d.charging())
    {
        current = 3;
    }
    else if (m_hadChargeThisVisit && d.seated())
    {
        current = 4;
    }
    else if (d.seated())
    {
        current = 2;
    }
    else if (d.handshaked())
    {
        current = 1;
    }
    else if (d.anyIrBlocked())
    {
        current = 0;
    }

    const int beforeCurrent = m_stageCurrent;
    const int beforeReached = m_stageReached;
    const bool beforeFaulted = m_stageFaulted;

    if (current == -1)
    {
        m_stageCurrent = -1;
        m_stageReached = -1;
    }
    else if (current == -2)
    {
        m_stageCurrent = -2;
    }
    else
    {
        m_stageCurrent = current;
        m_stageReached = qMax(m_stageReached, current);
    }

    if (beforeCurrent != m_stageCurrent || beforeReached != m_stageReached || beforeFaulted != m_stageFaulted)
        emit stageChanged();
}

// ------------------------------------------------------------------ 本地统计

void StationController::startSession()
{
    m_sessionEnergyKwh = 0.0;
    m_sessionSeconds = 0;
    m_sessionSecondsAcc = 0.0;
    m_sessionStartSoc = m_data.batterySoc;
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
    if (dt <= 0.0 || dt > 2.0)
        dt = 0.1; // 异常间隔（首次/卡顿）按一帧处理

    if (d.charging())
    {
        // 环回模式把 60 秒仿真成一个完整充电周期，时间指标需要按同一比例缩放，
        // 否则"已充时长"只有几十秒、而"预计剩余"算出来是几十分钟，自相矛盾。
        const double timeScale = m_client->isLoopbackEnabled() ? 60.0 : 1.0;
        m_sessionSecondsAcc += dt * timeScale;
        m_sessionSeconds = int(m_sessionSecondsAcc);

        // 充电量用 SOC 增量×额定容量估算：不依赖墙钟时间，
        // 与显示的 SOC、剩余时间用的是同一套容量模型，三者自洽。
        const double delta = (d.batterySoc - m_sessionStartSoc) / 100.0 * m_packCapacityKwh;
        m_sessionEnergyKwh = qMax(0.0, delta);
    }

    // 曲线：降采样后入队，避免 10Hz 打满
    if (++m_curveDivider >= kCurveKeepEvery)
    {
        m_curveDivider = 0;
        CurvePoint p;
        p.time = QDateTime::currentDateTime();
        p.currentA = d.chargeCurrentA();
        p.voltageV = d.voltageV();
        m_curve.append(p);
        while (m_curve.size() > kCurveMaxPoints)
            m_curve.remove(0);
    }
}

int StationController::remainingSeconds() const
{
    const double power = m_data.chargePowerKw();
    if (!m_data.charging() || power < 0.05)
        return -1;
    const double need = (100.0 - m_data.batterySoc) / 100.0 * m_packCapacityKwh;
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

// ------------------------------------------------------------------ 控制

void StationController::closeOutputRelay()
{
    m_client->sendRelay(1, 1);
}

void StationController::openOutputRelay()
{
    m_client->sendRelay(0, 0);
}
