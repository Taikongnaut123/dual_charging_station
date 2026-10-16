#ifndef BRIDGECLIENT_H
#define BRIDGECLIENT_H

#include "mdcdata.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVector>

class QUdpSocket;
class QTimer;
class QJsonObject;

/**
 * BridgeClient —— 与 mdc_bridge 之间的 UDP 通道
 *
 * 本程序**完全不依赖 ROS2**（不需要 rclcpp、不需要 source ROS 环境、
 * 不需要装 mdc_serial 包），只要有 mdc_bridge 在对端跑着就行。
 * 协议契约见 shared/mdc_protocol.h。
 *
 * 链路三态（能区分"桥没起来"和"设备没数据"，排障时非常关键）：
 *   NoBridge   —— 连心跳都收不到：桥进程未运行 / 网络不通
 *   BridgeOnly —— 桥在，但设备没上报：串口没接 / mdc_node 没起来
 *   Online     —— 正常收到状态帧
 */
class BridgeClient : public QObject
{
    Q_OBJECT

public:
    enum class LinkState
    {
        NoBridge,
        BridgeOnly,
        Online
    };
    Q_ENUM(LinkState)

    explicit BridgeClient(QObject *parent = nullptr);
    ~BridgeClient() override;

    /** 绑定本机状态接收端口并指定桥的地址；返回 false 表示端口绑定失败 */
    bool start(quint16 cmdPort = mdcproto::kDefaultCmdPort,
               quint16 statPort = mdcproto::kDefaultStatPort,
               const QString &bridgeHost = QStringLiteral("127.0.0.1"));
    void stop();

    LinkState linkState() const { return m_linkState; }
    bool isOnline() const { return m_linkState == LinkState::Online; }
    const MdcData &lastData() const { return m_data; }
    quint64 frameCount() const { return m_frameCount; }
    quint64 lostFrames() const { return m_lostFrames; }
    int frameRateHz() const { return m_frameRateHz; }
    QString bridgeHost() const { return m_bridgeHost.toString(); }
    quint16 commandPort() const { return m_cmdPort; }
    quint16 statusPort() const { return m_statPort; }

    static QString linkStateText(LinkState s);

    // ---------------------------------------------------------- 控制指令
    void sendRelay(quint8 relay1, quint8 relay2);
    void sendFan(quint8 fan1, quint8 fan2, quint8 fan3, quint8 fan4);
    void sendPowerio(quint8 ctrlMask);
    void sendWirelessCharge(const QVector<quint8> &payload);
    void sendLed(quint8 ch1Mode, quint8 ch1Duty, quint8 ch2Mode, quint8 ch2Duty,
                 quint8 ch3Mode, quint8 ch3Duty, quint8 rgbMode,
                 quint8 rDuty, quint8 gDuty, quint8 bDuty);

    /** 通用下发；返回本次请求的 id（应答会带同一个 id 回来） */
    int sendCommand(const QString &cmd, const QJsonObject &args = QJsonObject());

    // ---------------------------------------------------------- 环回模式
    /**
     * 环回模式：不需要真实桥，本机自造假数据驱动界面。
     * 用于开发阶段调试界面 / 演示，也可在一体屏上作为"无信号"时的兜底画面。
     * 打开后不会占用 UDP 端口，控制指令会被直接确认（不会真的下发）。
     */
    void setLoopbackEnabled(bool on);
    bool isLoopbackEnabled() const { return m_loopback; }

signals:
    void dataReceived(const MdcData &data);
    void linkStateChanged(BridgeClient::LinkState state);
    /** level: 0=普通 1=警告 2=错误 3=成功 */
    void commandFinished(int id, const QString &cmd, bool ok, const QString &message);
    void logMessage(int level, const QString &message);

private slots:
    void onReadyRead();
    void onWatchdog();
    void onLoopbackTick();

private:
    void setLinkState(LinkState s);
    void handleObject(const QJsonObject &o);
    void sendRaw(const QByteArray &payload);
    void generateLoopbackFrame();

    struct PendingCommand
    {
        QString cmd;
        qint64 deadlineMs = 0;
    };

    QUdpSocket *m_socket = nullptr;
    QTimer *m_watchdog = nullptr;
    QHostAddress m_bridgeHost;
    quint16 m_cmdPort = mdcproto::kDefaultCmdPort;
    quint16 m_statPort = mdcproto::kDefaultStatPort;

    MdcData m_data;
    LinkState m_linkState = LinkState::NoBridge;

    qint64 m_lastAnyPacketMs = 0;
    qint64 m_lastStatePacketMs = 0;
    qint64 m_lastHeartbeatMs = 0;
    int m_frameRateHz = 0;

    qint64 m_seqWindowStartMs = 0;
    int m_seqWindowCount = 0;
    qint64 m_lastSeq = -1;
    quint64 m_frameCount = 0;
    quint64 m_lostFrames = 0;

    int m_nextCommandId = 1;
    QHash<int, PendingCommand> m_pending;

    // 环回模式
    QTimer *m_loopbackTimer = nullptr;
    bool m_loopback = false;
    double m_loopbackT = 0.0; // 环回仿真时钟（秒）
};

#endif // BRIDGECLIENT_H
