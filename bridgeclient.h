#ifndef BRIDGECLIENT_H
#define BRIDGECLIENT_H

#include "mdcdata.h"

#include <QObject>
#include <QString>

class QUdpSocket;
class QTimer;
class QJsonObject;

/**
 * BridgeClient —— 从 mdc_bridge 接收 UDP JSON 的**单向**通道
 *
 * 本程序**完全不依赖 ROS2**（不需要 rclcpp、不需要 source ROS 环境、
 * 不需要装 mdc_serial 包），只要有 mdc_bridge 在往本机端口发数据就行。
 * 协议契约见 shared/mdc_protocol.h。
 *
 * 按 doc/MDC接口使用指南.md，充电桩协议是"只收不发"：充电控制由充电桩主机
 * 自主完成，mdc_node 不提供下行 Service。所以这里**没有任何发送代码**，
 * 界面也不需要知道桥的地址——只管绑定端口等数据。
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

    /** 绑定本机接收端口；返回 false 表示端口绑定失败（通常是已有实例在跑） */
    bool start(quint16 statPort = mdcproto::kDefaultStatPort);
    void stop();

    LinkState linkState() const { return m_linkState; }
    bool isOnline() const { return m_linkState == LinkState::Online; }
    const MdcData &lastData() const { return m_data; }
    quint64 frameCount() const { return m_frameCount; }
    quint64 lostFrames() const { return m_lostFrames; }
    int frameRateHz() const { return m_frameRateHz; }
    quint16 statusPort() const { return m_statPort; }

    static QString linkStateText(LinkState s);

    // ---------------------------------------------------------- 环回模式
    /**
     * 环回模式：不需要真实桥，本机自造假数据驱动界面。
     * 用于开发阶段调试界面 / 演示，也可在一体屏上作为"无信号"时的兜底画面。
     * 打开后不会占用 UDP 端口。
     */
    void setLoopbackEnabled(bool on);
    bool isLoopbackEnabled() const { return m_loopback; }

signals:
    void dataReceived(const MdcData &data);
    void linkStateChanged(BridgeClient::LinkState state);
    /** level: 0=普通 1=警告 2=错误 3=成功 */
    void logMessage(int level, const QString &message);

private slots:
    void onReadyRead();
    void onWatchdog();
    void onLoopbackTick();

private:
    void setLinkState(LinkState s);
    void handleObject(const QJsonObject &o);
    void generateLoopbackFrame(double t);

    QUdpSocket *m_socket = nullptr;
    QTimer *m_watchdog = nullptr;
    QTimer *m_loopbackTimer = nullptr;

    quint16 m_statPort = mdcproto::kDefaultStatPort;
    MdcData m_data;
    LinkState m_linkState = LinkState::NoBridge;

    qint64 m_lastAnyPacketMs = 0;
    qint64 m_lastStatePacketMs = 0;
    /** 是否真的收到过任何报文。区分"从未收到"与"收到过但超时" */
    bool m_gotAnyPacket = false;
    int m_frameRateHz = 0;

    qint64 m_seqWindowStartMs = 0;
    int m_seqWindowCount = 0;
    qint64 m_lastSeq = -1;
    quint64 m_frameCount = 0;
    quint64 m_lostFrames = 0;

    bool m_loopback = false;
    double m_loopbackT = 0.0;
};

#endif // BRIDGECLIENT_H
