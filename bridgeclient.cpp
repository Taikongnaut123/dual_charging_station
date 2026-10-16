#include "bridgeclient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>
#include <QtMath>

namespace
{
    qint64 monotonicMs()
    {
        return QDateTime::currentMSecsSinceEpoch();
    }
    constexpr int kWatchdogIntervalMs = 250;
    constexpr int kNoBridgeTimeoutMs = 3000; // 这么久收不到任何包 -> 判定没桥
    constexpr int kStateTimeoutMs = 1200;    // 这么久收不到状态帧 -> 降级为"只有桥"
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
    m_loopbackTimer->setInterval(100); // 与真实设备一致的 10Hz
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

bool BridgeClient::start(quint16 cmdPort, quint16 statPort, const QString &bridgeHost)
{
    stop();

    m_cmdPort = cmdPort;
    m_statPort = statPort;
    m_bridgeHost = QHostAddress(bridgeHost);

    // 故意不使用 ShareAddress/ReuseAddressHint：
    // 允许端口共享时，第二个实例能"惄惄"绑上同一端口，结果数据报被其中
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
    m_lastHeartbeatMs = now;
    m_seqWindowStartMs = now;
    m_seqWindowCount = 0;
    m_frameCount = 0;
    m_lostFrames = 0;
    m_lastSeq = -1;
    m_frameRateHz = 0;
    m_nextCommandId = 1;
    m_pending.clear();

    m_watchdog->start();

    emit logMessage(0, QStringLiteral("UDP 通道已就绪：监听 %1，桥地址 %2:%3")
                           .arg(m_statPort)
                           .arg(bridgeHost)
                           .arg(m_cmdPort));
    setLinkState(LinkState::NoBridge);
    return true;
}

void BridgeClient::stop()
{
    m_watchdog->stop();
    // 同理，这里只能用 state() 判断，不能依赖 isOpen()
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
    m_pending.clear();
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
        m_lastAnyPacketMs = now;
        m_lastHeartbeatMs = now;
        const bool mdcAlive = o.value(QLatin1String(mdcproto::kMdcAlive)).toInt() != 0;
        // 心跳里带着"设备是否上报"，据此区分"桥在但设备没数据"
        if (!mdcAlive && now - m_lastStatePacketMs > kStateTimeoutMs)
            setLinkState(LinkState::BridgeOnly);
        else if (mdcAlive)
            setLinkState(LinkState::Online);
        return;
    }

    if (type == QLatin1String(mdcproto::kTypeAck))
    {
        m_lastAnyPacketMs = now;
        const int id = o.value(QLatin1String(mdcproto::kId)).toInt();
        const QString cmd = o.value(QLatin1String(mdcproto::kCmd)).toString();
        const bool ok = o.value(QLatin1String(mdcproto::kOk)).toBool();
        const QString msg = o.value(QLatin1String(mdcproto::kMsg)).toString();

        m_pending.remove(id);
        emit commandFinished(id, cmd, ok, msg);
        emit logMessage(ok ? 3 : 2,
                        QStringLiteral("%1 指令%2%3")
                            .arg(cmd, ok ? QStringLiteral("下发成功") : QStringLiteral("下发失败"),
                                 msg.isEmpty() ? QString()
                                               : QStringLiteral("：") + msg));
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
    if (now - m_lastAnyPacketMs > kNoBridgeTimeoutMs)
        setLinkState(LinkState::NoBridge);
    else if (now - m_lastStatePacketMs > kStateTimeoutMs)
        setLinkState(LinkState::BridgeOnly);

    // 3) 控制指令应答超时
    QList<int> expired;
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it)
    {
        if (now > it.value().deadlineMs)
            expired.append(it.key());
    }
    for (int id : expired)
    {
        const QString cmd = m_pending.value(id).cmd;
        m_pending.remove(id);
        emit commandFinished(id, cmd, false, QStringLiteral("应答超时（%1 ms）").arg(mdcproto::kAckTimeoutMs));
        emit logMessage(2, QStringLiteral("%1 指令应答超时").arg(cmd));
    }
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
        m_watchdog->stop();
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
    m_loopbackT += 0.1;
    if (m_loopbackT > 60.0)
        m_loopbackT = 0.0;

    generateLoopbackFrame();
    ++m_frameCount;
    m_frameRateHz = 10;
    emit dataReceived(m_data);
}

/**
 * 生成一帧假数据，60 秒走完一个完整充电周期：
 *   0-3s 无车 / 3-5s 停车检测 / 5-6.5s 握手 / 6.5-8s 落座 /
 *   8-50s 充电(38%->100%) / 50-55s 充电完成 / 55-60s 离桩
 */
void BridgeClient::generateLoopbackFrame()
{
    const double t = m_loopbackT;
    MdcData d;

    const bool detecting = (t >= 3.0 && t < 6.5);
    const bool handshaked = (t >= 5.0 && t < 55.0);
    const bool seated = (t >= 6.5 && t < 55.0);
    const bool charging = (t >= 8.0 && t < 50.0);
    const bool finished = (t >= 50.0 && t < 55.0);

    double soc = 38.0;
    double chargeMa = 0.0;
    double dischargeMa = 0.0;

    if (charging)
    {
        const double span = 50.0 - 8.0;
        const double p = (t - 8.0) / span;
        const double cvStart = (80.0 - 38.0) / (100.0 - 38.0);
        if (p < cvStart)
        {
            soc = 38.0 + (80.0 - 38.0) * (p / cvStart);
            chargeMa = 30000.0;
        }
        else
        {
            const double q = (p - cvStart) / (1.0 - cvStart);
            soc = 80.0 + (100.0 - 80.0) * q;
            chargeMa = 30000.0 * (1.0 - 0.92 * q);
        }
    }
    else if (t >= 50.0)
    {
        soc = 100.0;
    }
    else
    {
        soc = qMax(20.0, 38.0 - 0.3 * t);
        dischargeMa = detecting ? 0.0 : 3200.0;
    }

    d.batterySoc = int(soc + 0.5);
    d.batteryChargeCurrent = int(chargeMa);
    d.batteryDischargeCurrent = int(dischargeMa);
    d.batteryTotalVoltage = int(40000.0 + 184.0 * d.batterySoc + chargeMa * 0.03);
    d.batteryTemp = int(26.0 + (charging ? 6.0 : 1.0) + 1.5 * qSin(t * 0.5));

    d.pmcTotalVoltage = int(48000.0 + 200.0 * qSin(t * 1.1));
    d.pmcTotalCurrent = int(chargeMa + (detecting ? 1200.0 : 600.0));
    d.pmcHeadVoltage = int(48000.0 + 150.0 * qSin(t * 1.7));
    d.pmcHeadCurrent = int((detecting || !seated) ? 820.0 : 180.0);
    d.pmcRightVoltage = int(47900.0 + 160.0 * qSin(t * 1.3 + 1.0));
    d.pmcRightCurrent = int((detecting || !seated) ? 1450.0 : 220.0);
    d.pmcLeftVoltage = int(48050.0 + 140.0 * qSin(t * 0.9 + 2.0));
    d.pmcLeftCurrent = int((detecting || !seated) ? 1390.0 : 210.0);
    d.pmcNonisolatedVoltage = int(12000.0 + 30.0 * qSin(t * 2.3));
    d.pmcNonisolatedCurrent = int(900.0 + (charging ? 700.0 : 0.0) + 60.0 * qSin(t * 0.8));
    d.pmcIsolatedCurrent = int(320.0 + 40.0 * qSin(t * 1.9));
    d.pmcAlarmStatus = 0;
    d.pmcEmergencyStatus = 0;

    d.powerioVo1 = 0;
    d.powerioVo2 = 0;
    d.powerioVo3 = 1;
    d.powerioVo4 = 1;
    d.powerioSw1 = 1;
    d.powerioSw2 = 0;
    d.powerioRelay1 = charging ? 1 : 0;
    d.powerioRelay2 = charging ? 1 : 0;
    d.powerioEcho1 = seated ? 1 : 0;
    d.robotSeated = seated ? 1 : 0;

    d.irBs = charging ? 1 : 0;
    d.irLA = (detecting || handshaked) ? 1 : 0;
    d.irLB = handshaked ? 1 : 0;
    d.irRA = (t >= 4.0 && t < 55.0) ? 1 : 0;
    d.irRB = handshaked ? 1 : 0;
    d.irSh = handshaked ? 1 : 0;

    const int fan = charging ? 80 : (finished ? 40 : 0);
    d.fan1Pwm = quint8(fan);
    d.fan2Pwm = quint8(fan);
    d.fan3Pwm = quint8(fan / 2);
    d.fan4Pwm = quint8(fan / 4);
    d.fanTemp = d.batteryTemp + 4;

    const int level = d.batterySoc * 8 / 100;
    for (int i = 0; i < d.ledCh1Data.size(); ++i)
        d.ledCh1Data[i] = quint8(i < level ? 255 : 0);
    for (int i = 0; i < d.ledCh2Data.size(); ++i)
        d.ledCh2Data[i] = quint8(charging ? 255 : 0);
    for (int i = 0; i < d.ledCh3Data.size(); ++i)
        d.ledCh3Data[i] = quint8(handshaked ? 180 : 0);

    d.ledRgbData[1] = quint8(charging ? 200 : 0);
    d.ledRgbData[2] = quint8(charging ? 60 : 0);
    d.ledRgbData[3] = quint8(charging ? 100 : 30);
    d.ledBrightness = 60;

    const int beat = int(t) % 6;
    d.wchRequestFlag = charging ? 1 : 0;
    d.wchBusy = (charging && beat < 2) ? 1 : 0;
    d.wchResult = charging ? quint8(beat < 2 ? 1 : 2) : 0;
    d.wchTxData[0] = 0x01;
    d.wchTxData[1] = quint8(d.batterySoc);
    d.wchTxData[2] = 0x01;
    d.wchTxData[3] = 0x01;
    d.wchRxData[0] = 0x02;
    d.wchRxData[1] = quint8(d.batterySoc);
    d.wchRxData[3] = quint8(charging ? 0x01 : 0x00);
    d.wchUpdateTick = quint32(t * 1000.0);

    m_data = d;
}

// ------------------------------------------------------------------ 发送

void BridgeClient::sendRaw(const QByteArray &payload)
{
    if (!m_socket)
        return;

    // 注意：这里**不能**用 m_socket->isOpen() 做前置判断。
    // 对「已 bind 但未 connect」的 QUdpSocket，isOpen() 在 Qt 5.15 上返回 false，
    // 而 state() 其实是 BoundState(4)、localPort() 也正常。
    // 用它当门槛会导致指令被静默丢弃（接收不受影响，表象是"能收不能发"）。
    // 唯一可靠的判据是 writeDatagram() 的返回值。
    const qint64 sent = m_socket->writeDatagram(payload, m_bridgeHost, m_cmdPort);
    if (sent != payload.size())
    {
        emit logMessage(2, QStringLiteral("向 %1:%2 发送失败：%3")
                               .arg(m_bridgeHost.toString())
                               .arg(m_cmdPort)
                               .arg(m_socket->errorString()));
    }
}

int BridgeClient::sendCommand(const QString &cmd, const QJsonObject &args)
{
    const int id = m_nextCommandId++;

    // 环回模式：不真的下发，延迟一小会儿后回一个"成功"，
    // 这样界面的按钮反馈链路也能被完整地开发与验证。
    if (m_loopback)
    {
        emit logMessage(0, QStringLiteral("下发指令 #%1：%2（环回模式，未真实下发）")
                               .arg(id)
                               .arg(cmd));
        QTimer::singleShot(80, this, [this, id, cmd]
                           { emit commandFinished(id, cmd, true,
                                                  QStringLiteral("环回模式：指令已模拟执行")); });
        return id;
    }

    QJsonObject o;
    o[QLatin1String(mdcproto::kType)] = QLatin1String(mdcproto::kTypeCommand);
    o[QLatin1String(mdcproto::kId)] = id;
    o[QLatin1String(mdcproto::kCmd)] = cmd;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
        o.insert(it.key(), it.value());

    PendingCommand p;
    p.cmd = cmd;
    p.deadlineMs = monotonicMs() + mdcproto::kAckTimeoutMs;
    m_pending.insert(id, p);

    sendRaw(QJsonDocument(o).toJson(QJsonDocument::Compact));
    emit logMessage(0, QStringLiteral("下发指令 #%1：%2").arg(id).arg(cmd));
    return id;
}

void BridgeClient::sendRelay(quint8 relay1, quint8 relay2)
{
    QJsonObject a;
    a[QStringLiteral("relay1")] = int(relay1);
    a[QStringLiteral("relay2")] = int(relay2);
    sendCommand(QLatin1String(mdcproto::kCmdRelay), a);
}

void BridgeClient::sendFan(quint8 fan1, quint8 fan2, quint8 fan3, quint8 fan4)
{
    QJsonObject a;
    a[QStringLiteral("fan1")] = int(fan1);
    a[QStringLiteral("fan2")] = int(fan2);
    a[QStringLiteral("fan3")] = int(fan3);
    a[QStringLiteral("fan4")] = int(fan4);
    sendCommand(QLatin1String(mdcproto::kCmdFan), a);
}

void BridgeClient::sendPowerio(quint8 ctrlMask)
{
    QJsonObject a;
    a[QStringLiteral("ctrl")] = int(ctrlMask); // 位掩码 1=关 0=开
    sendCommand(QLatin1String(mdcproto::kCmdPowerio), a);
}

void BridgeClient::sendWirelessCharge(const QVector<quint8> &payload)
{
    QJsonArray arr;
    for (int i = 0; i < 4; ++i)
        arr.append(int(i < payload.size() ? payload[i] : 0));

    QJsonObject a;
    a[QStringLiteral("payload")] = arr;
    sendCommand(QLatin1String(mdcproto::kCmdWch), a);
}

void BridgeClient::sendLed(quint8 ch1Mode, quint8 ch1Duty, quint8 ch2Mode, quint8 ch2Duty,
                           quint8 ch3Mode, quint8 ch3Duty, quint8 rgbMode,
                           quint8 rDuty, quint8 gDuty, quint8 bDuty)
{
    QJsonObject a;
    a[QStringLiteral("ch1_mode")] = int(ch1Mode);
    a[QStringLiteral("ch1_duty")] = int(ch1Duty);
    a[QStringLiteral("ch2_mode")] = int(ch2Mode);
    a[QStringLiteral("ch2_duty")] = int(ch2Duty);
    a[QStringLiteral("ch3_mode")] = int(ch3Mode);
    a[QStringLiteral("ch3_duty")] = int(ch3Duty);
    a[QStringLiteral("rgb_mode")] = int(rgbMode);
    a[QStringLiteral("r_duty")] = int(rDuty);
    a[QStringLiteral("g_duty")] = int(gDuty);
    a[QStringLiteral("b_duty")] = int(bDuty);
    sendCommand(QLatin1String(mdcproto::kCmdLed), a);
}
