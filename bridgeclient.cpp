#include "bridgeclient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUdpSocket>
#include <QtMath>

namespace
{
    qint64 monotonicMs()
    {
        return QDateTime::currentMSecsSinceEpoch();
    }

    constexpr int kWatchdogIntervalMs = 250;
    constexpr int kNoBridgeTimeoutMs = 3000; // 这么久收不到任何包 -> 判定没桥
    // 设备约 500ms 一帧且"仅在有新增帧时发布"，实际间隔可能更长，留到 2.5 秒
    constexpr int kStateTimeoutMs = 2500;

    /** 环回模式一个完整循环（墙钟秒）——真机是 500ms/帧，这里压缩以便演示 */
    constexpr double kLoopbackCycle = 40.0;
    constexpr double kLoopbackIdleManualSoc = 38.0;
    constexpr double kLoopbackIdleAutoSoc = 45.0;

    /** 充电中：SOC 线性爬升；电流前 80% 恒流，后 20% 线性回落（单位 0.1A） */
    void chargeProfile(double p, double *soc, double *deciA)
    {
        *soc = 38.0 + 62.0 * p;
        if (p < 0.8)
            *deciA = 150.0;
        else
            *deciA = 150.0 - 130.0 * ((p - 0.8) / 0.2);
    }

    quint16 packVoltage(double soc)
    {
        return quint16(46000.0 + 80.0 * soc);
    }

} // namespace

BridgeClient::BridgeClient(QObject *parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &BridgeClient::onReadyRead);

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(kWatchdogIntervalMs);
    connect(m_watchdog, &QTimer::timeout, this, &BridgeClient::onWatchdog);

    m_loopbackTimer = new QTimer(this);
    m_loopbackTimer->setInterval(500); // 与真机一致的约 2Hz
    connect(m_loopbackTimer, &QTimer::timeout, this, &BridgeClient::onLoopbackTick);
}

BridgeClient::~BridgeClient()
{
    stop();
}

QString BridgeClient::linkStateText(LinkState s)
{
    switch (s)
    {
    case LinkState::Online:
        return QStringLiteral("在线");
    case LinkState::BridgeOnly:
        return QStringLiteral("桥已连接");
    case LinkState::NoBridge:
        return QStringLiteral("未连接");
    }
    return QStringLiteral("未知");
}

bool BridgeClient::start(quint16 statPort)
{
    stop();

    m_statPort = statPort;

    // 故意不使用 ShareAddress/ReuseAddressHint：
    // 允许端口共享时，第二个实例能"悄悄"绑上同一端口，结果数据报被其中
    // 一个进程全部收走，表现为界面永远 0 帧 —— 极难排查。宁可在这里直接报错。
    if (!m_socket->bind(QHostAddress::AnyIPv4, m_statPort))
    {
        emit logMessage(2, QStringLiteral("绑定 UDP %1 失败：%2（是否已有另一个实例在运行？）")
                               .arg(m_statPort)
                               .arg(m_socket->errorString()));
        return false;
    }

    const qint64 now = monotonicMs();
    m_lastAnyPacketMs = now;
    m_lastStatePacketMs = now;
    m_gotAnyPacket = false;
    m_seqWindowStartMs = now;
    m_seqWindowCount = 0;
    m_frameCount = 0;
    m_lostFrames = 0;
    m_lastSeq = -1;
    m_frameRateHz = 0;

    m_watchdog->start();

    emit logMessage(0, QStringLiteral("UDP 通道已就绪：监听 %1（单向接收，不需要知道桥的地址）")
                           .arg(m_statPort));
    setLinkState(LinkState::NoBridge);
    return true;
}

void BridgeClient::stop()
{
    m_watchdog->stop();
    // 只能用 state() 判断，不能依赖 isOpen()：
    // 对「已 bind 但未 connect」的 QUdpSocket，Qt 5.15 上 isOpen() 返回 false。
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
}

// ------------------------------------------------------------------ 接收

void BridgeClient::onReadyRead()
{
    while (m_socket->hasPendingDatagrams())
    {
        QByteArray payload;
        payload.resize(int(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(payload.data(), payload.size());

        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
        {
            emit logMessage(1, QStringLiteral("收到无法解析的报文（%1 字节）")
                                   .arg(payload.size()));
            continue;
        }
        handleObject(doc.object());
    }
}

void BridgeClient::handleObject(const QJsonObject &o)
{
    const qint64 now = monotonicMs();
    const QString type = o.value(QLatin1String(mdcproto::kType)).toString();

    if (type == QLatin1String(mdcproto::kTypeState))
    {
        m_gotAnyPacket = true;
        m_lastAnyPacketMs = now;
        m_lastStatePacketMs = now;
        ++m_frameCount;
        ++m_seqWindowCount;

        // 用 seq 估算丢包（UDP 无重传，这个数字很有参考价值）
        const qint64 seq = qint64(o.value(QLatin1String(mdcproto::kSeq)).toDouble());
        if (m_lastSeq >= 0 && seq > m_lastSeq + 1)
            m_lostFrames += quint64(seq - m_lastSeq - 1);
        m_lastSeq = seq;

        m_data = MdcData::fromJson(o);
        emit dataReceived(m_data);
        setLinkState(LinkState::Online);
        return;
    }

    if (type == QLatin1String(mdcproto::kTypeHeartbeat))
    {
        m_gotAnyPacket = true;
        m_lastAnyPacketMs = now;
        const bool mdcAlive = o.value(QLatin1String(mdcproto::kMdcAlive)).toInt() != 0;
        // 心跳里带着"设备是否上报"，据此区分"桥在但设备没数据"
        if (!mdcAlive && now - m_lastStatePacketMs > kStateTimeoutMs)
            setLinkState(LinkState::BridgeOnly);
        else if (mdcAlive)
            setLinkState(LinkState::Online);
        return;
    }

    m_lastAnyPacketMs = now;
    emit logMessage(1, QStringLiteral("收到未知报文类型：%1").arg(type));
}

// ------------------------------------------------------------------ 看门狗

void BridgeClient::onWatchdog()
{
    const qint64 now = monotonicMs();

    // 1) 帧率统计（1 秒滑动窗口）
    if (now - m_seqWindowStartMs >= 1000)
    {
        m_frameRateHz = m_seqWindowCount;
        m_seqWindowCount = 0;
        m_seqWindowStartMs = now;
    }

    // 2) 链路状态降级
    //
    // 注意 `!m_gotAnyPacket` 这个条件不能省：刚启动时从来没收到过任何包，
    // 而 m_lastStatePacketMs 是在 start() 里初始化的，所以 2.5 秒后
    // "now - m_lastStatePacketMs > kStateTimeoutMs" 会成立，凭空报出
    // "桥已连接"（它的语义是收到心跳但设备无数据）——完全不符合事实。
    if (!m_gotAnyPacket || now - m_lastAnyPacketMs > kNoBridgeTimeoutMs)
        setLinkState(LinkState::NoBridge);
    else if (now - m_lastStatePacketMs > kStateTimeoutMs)
        setLinkState(LinkState::BridgeOnly);
}

void BridgeClient::setLinkState(LinkState s)
{
    if (m_linkState == s)
        return;
    m_linkState = s;
    emit linkStateChanged(m_linkState);
}

// ------------------------------------------------------------------ 环回模式

void BridgeClient::setLoopbackEnabled(bool on)
{
    if (m_loopback == on)
        return;
    m_loopback = on;

    if (on)
    {
        stop(); // 环回模式不占用真实 UDP 端口
        m_loopbackT = 0.0;
        m_frameCount = 0;
        m_lostFrames = 0;
        m_lastSeq = -1;
        m_loopbackTimer->start();
        emit logMessage(1, QStringLiteral("已进入环回模式：当前数据为内置仿真，未连接真实桥"));
        setLinkState(LinkState::Online);
    }
    else
    {
        m_loopbackTimer->stop();
        setLinkState(LinkState::NoBridge);
        emit logMessage(0, QStringLiteral("已退出环回模式"));
    }
}

void BridgeClient::onLoopbackTick()
{
    m_loopbackT += 0.5;
    if (m_loopbackT >= kLoopbackCycle)
        m_loopbackT = 0.0;

    generateLoopbackFrame(m_loopbackT);
    ++m_frameCount;
    m_frameRateHz = 2;
    emit dataReceived(m_data);
}

/**
 * 生成一帧假数据，40 秒走完一个完整循环：
 *   0-2.5s  CCU 初始化 -> 回零 -> 到位（hall 触发）
 *   2.5-3s  就位空闲
 *   3-14s   手动口充电（38% -> 100%）
 *   14-17s  手动口充满
 *   17-19s  空闲
 *   19-30s  自动口充电（45% -> 100%）
 *   30-33s  自动口充满
 *   33-40s  空闲
 */
void BridgeClient::generateLoopbackFrame(double t)
{
    MdcData d;

    const bool manCharging = (t >= 3.0 && t < 14.0);
    const bool manFull = (t >= 14.0 && t < 17.0);
    const bool autoCharging = (t >= 19.0 && t < 30.0);
    const bool autoFull = (t >= 30.0 && t < 33.0);

    // ---- 手动充电口 ----
    double manSoc = kLoopbackIdleManualSoc;
    double manDeciA = 0.0;
    if (manCharging)
        chargeProfile((t - 3.0) / 11.0, &manSoc, &manDeciA);
    else if (manFull)
        manSoc = 100.0;

    d.manualChgSwitch = (manCharging || manFull) ? 1 : 0;
    d.manualBatteryId = 1;
    d.manualTotalVoltage = packVoltage(manSoc);
    d.manualChargeCurrent = quint16(manDeciA);
    d.manualDischargeCurrent = 0;
    d.manualSoc = quint16(manSoc + 0.5);
    d.manualTemp = quint16(26.0 + (manCharging ? 8.0 : 1.0) + 1.5 * qSin(t * 0.4));

    // ---- 自动充电口 ----
    double autoSoc = kLoopbackIdleAutoSoc;
    double autoDeciA = 0.0;
    if (autoCharging)
        chargeProfile((t - 19.0) / 11.0, &autoSoc, &autoDeciA);
    else if (autoFull)
        autoSoc = 100.0;

    d.autoChgSwitch = (autoCharging || autoFull) ? 1 : 0;
    d.autoBatteryId = 2;
    d.autoTotalVoltage = packVoltage(autoSoc);
    d.autoChargeCurrent = quint16(autoDeciA);
    d.autoDischargeCurrent = 0;
    d.autoSoc = quint16(autoSoc + 0.5);
    d.autoTemp = quint16(26.0 + (autoCharging ? 8.0 : 1.0) + 1.5 * qSin(t * 0.4 + 2.0));

    // ---- 充电桩状态 ----
    if (t < 0.5)
        d.ccuMotorStatus = 1; // 初始化中
    else if (t < 1.0)
        d.ccuMotorStatus = 2; // 初始化完成
    else if (t < 1.5)
        d.ccuMotorStatus = 3; // 回零中
    else if (t < 2.0)
        d.ccuMotorStatus = 4; // 回零完成
    else if (t < 2.5)
        d.ccuMotorStatus = 5; // 到位中
    else
        d.ccuMotorStatus = 6; // 到位完成

    if (manCharging || manFull)
        d.chargeMode = 1;
    else if (autoCharging || autoFull)
        d.chargeMode = 2;
    else
        d.chargeMode = 0;

    if (manCharging || autoCharging)
        d.chargeState = 2;
    else if (manFull || autoFull)
        d.chargeState = 3;
    else
        d.chargeState = 1;

    d.fanStatus = (manCharging || autoCharging || manFull || autoFull) ? 1 : 0;
    d.hallSwitch = (t >= 2.5) ? 1 : 0;

    d.manualLinkStatus = (manCharging || manFull) ? 1 : 0;
    d.manualChargeStatus = manCharging ? 1 : 0;
    d.autoLinkStatus = (autoCharging || autoFull) ? 1 : 0;
    d.autoChargeStatus = autoCharging ? 1 : 0;

    m_data = d;
}
