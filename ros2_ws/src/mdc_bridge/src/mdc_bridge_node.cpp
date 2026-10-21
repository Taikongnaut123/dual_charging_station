/**
 * mdc_bridge —— ROS2 与充电桩显示屏之间的桥（**单向：只收不发**）
 *
 *   /mdc_data (约 2Hz)  ──订阅──►  序列化成 JSON  ──UDP──►  界面程序 (Qt)
 *
 * 设计要点：
 *  1. 按 doc/MDC接口使用指南.md，充电桩协议是**只收不发**：充电控制由充电桩
 *     主机自主完成，mdc_node 不提供任何下行 Service。所以桥只做一件事——
 *     把遥测转发给界面，没有任何下行链路。
 *  2. 只依赖 rclcpp + mdc_serial，不依赖 Qt，也不依赖第三方 JSON 库
 *     （协议是扁平的，手写序列化几十行搞定，见 simple_json.h）。
 *  3. 界面程序因此可以零 ROS2 依赖，部署到没装 ROS2 的一体屏上。
 *
 * 不需要 bind 本地端口：界面只监听、从不回包，所以桥只管往 peer_host:peer_port
 * 发就行（sendto 会自动分配临时源端口）。
 */

#include <rclcpp/rclcpp.hpp>

#include <mdc_serial/msg/mdc_data.hpp>

#include "mdc_protocol.h"
#include "simple_json.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>

namespace
{

    constexpr const char *kNodeName = "mdc_bridge";

    double nowSec()
    {
        using namespace std::chrono;
        return duration<double>(system_clock::now().time_since_epoch()).count();
    }

    int64_t steadyMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

} // namespace

class MdcBridge : public rclcpp::Node
{
public:
    MdcBridge()
        : rclcpp::Node(kNodeName)
    {
        // ---------------------------------------------------------- 参数
        m_peerHost = declare_parameter<std::string>("peer_host", std::string("127.0.0.1"));
        const int peerPort = declare_parameter<int>("peer_port", mdcproto::kDefaultStatPort);
        const std::string topic = declare_parameter<std::string>("topic", std::string("/mdc_data"));

        // ---------------------------------------------------------- UDP
        if (!initUdp(peerPort))
        {
            RCLCPP_FATAL(get_logger(), "UDP 初始化失败，桥无法工作");
            rclcpp::shutdown();
            return;
        }

        // ---------------------------------------------------------- 订阅
        // 用 SensorDataQoS（BEST_EFFORT）而不是默认的 RELIABLE：
        // DDS 里 BEST_EFFORT 订阅者与两种可靠性的发布者都兼容，
        // 而 RELIABLE 订阅者遇到 BEST_EFFORT 发布者会连不上（表现为一帧都收不到，
        // 且没有明显报错）。设备遥测丢一两帧无所谓，兼容性更要紧。
        m_sub = create_subscription<mdc_serial::msg::MdcData>(
            topic, rclcpp::SensorDataQoS(),
            [this](mdc_serial::msg::MdcData::ConstSharedPtr msg)
            { onData(msg); });

        m_hbTimer = create_wall_timer(std::chrono::seconds(1), [this]
                                      { sendHeartbeat(); });

        RCLCPP_INFO(get_logger(), "桥已启动（单向：只把设备数据转发给界面）");
        RCLCPP_INFO(get_logger(), "  订阅话题      : %s", topic.c_str());
        RCLCPP_INFO(get_logger(), "  状态发送目标  : UDP %s:%d", m_peerHost.c_str(), peerPort);
    }

    ~MdcBridge() override
    {
        if (m_sock >= 0)
            ::close(m_sock);
    }

private:
    // ================================================================ 上行
    void onData(const mdc_serial::msg::MdcData::ConstSharedPtr &m)
    {
        m_lastDataMs = steadyMs();
        sendToPeer(buildState(*m));
    }

    std::string buildState(const mdc_serial::msg::MdcData &m)
    {
        sjson::Builder b;
        b.str(mdcproto::kType, mdcproto::kTypeState)
            .num(mdcproto::kSeq, static_cast<long long>(++m_seq))
            .dbl(mdcproto::kTs, nowSec())

            // ---- 手动充电口 ----
            .num(mdcproto::kManSwitch, m.manual_chg_switch)
            .num(mdcproto::kManBatteryId, m.manual_battery_id)
            .num(mdcproto::kManVoltage, m.manual_total_voltage)
            .num(mdcproto::kManChargeCurrent, m.manual_charge_current)
            .num(mdcproto::kManDischargeCurrent, m.manual_discharge_current)
            .num(mdcproto::kManSoc, m.manual_soc)
            .num(mdcproto::kManTemp, m.manual_temp)

            // ---- 自动充电口 ----
            .num(mdcproto::kAutoSwitch, m.auto_chg_switch)
            .num(mdcproto::kAutoBatteryId, m.auto_battery_id)
            .num(mdcproto::kAutoVoltage, m.auto_total_voltage)
            .num(mdcproto::kAutoChargeCurrent, m.auto_charge_current)
            .num(mdcproto::kAutoDischargeCurrent, m.auto_discharge_current)
            .num(mdcproto::kAutoSoc, m.auto_soc)
            .num(mdcproto::kAutoTemp, m.auto_temp)

            // ---- 充电桩状态 ----
            .num(mdcproto::kCcuMotorStatus, m.ccu_motor_status)
            .num(mdcproto::kChargeMode, m.charge_mode)
            .num(mdcproto::kChargeState, m.charge_state)
            .num(mdcproto::kFanStatus, m.fan_status)
            .num(mdcproto::kHallSwitch, m.hall_switch)
            .num(mdcproto::kManualLink, m.manual_link_status)
            .num(mdcproto::kManualCharge, m.manual_charge_status)
            .num(mdcproto::kAutoLink, m.auto_link_status)
            .num(mdcproto::kAutoCharge, m.auto_charge_status);

        return b.done();
    }

    void sendHeartbeat()
    {
        const bool mdcAlive = (steadyMs() - m_lastDataMs) < mdcproto::kMdcStaleMs;
        if (mdcAlive != m_lastAlive)
        {
            m_lastAlive = mdcAlive;
            if (mdcAlive)
                RCLCPP_INFO(get_logger(), "设备数据恢复正常");
            else
                RCLCPP_WARN(get_logger(), "超过 %d ms 未收到 /mdc_data，设备可能失联",
                            mdcproto::kMdcStaleMs);
        }

        sjson::Builder b;
        b.str(mdcproto::kType, mdcproto::kTypeHeartbeat)
            .num(mdcproto::kRosAlive, 1)
            .num(mdcproto::kMdcAlive, mdcAlive ? 1 : 0)
            .dbl(mdcproto::kTs, nowSec());
        sendToPeer(b.done());
    }

    // ================================================================ UDP
    bool initUdp(int peerPort)
    {
        m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock < 0)
        {
            RCLCPP_ERROR(get_logger(), "socket() 失败: %s", std::strerror(errno));
            return false;
        }

        m_peer.sin_family = AF_INET;
        m_peer.sin_port = htons(static_cast<uint16_t>(peerPort));
        if (::inet_pton(AF_INET, m_peerHost.c_str(), &m_peer.sin_addr) != 1)
        {
            RCLCPP_ERROR(get_logger(), "peer_host 不是合法 IPv4 地址: %s", m_peerHost.c_str());
            return false;
        }

        m_havePeer = true;
        return true;
    }

    void sendToPeer(const std::string &payload)
    {
        if (!m_havePeer || m_sock < 0)
            return;

        // UDP 无连接，这里只判断返回值；失败也不要刷屏（界面不在线时会一直失败）
        ::sendto(m_sock, payload.data(), payload.size(), 0,
                 reinterpret_cast<const sockaddr *>(&m_peer), sizeof(m_peer));
    }

    // ================================================================ 成员
    std::string m_peerHost;

    int m_sock = -1;
    sockaddr_in m_peer{};
    bool m_havePeer = false;

    unsigned long long m_seq = 0;
    int64_t m_lastDataMs = -100000;
    bool m_lastAlive = false;

    rclcpp::Subscription<mdc_serial::msg::MdcData>::SharedPtr m_sub;
    rclcpp::TimerBase::SharedPtr m_hbTimer;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);

    // 没有下行链路了，单线程 spin 足够：订阅回调里只做序列化 + 一次 sendto
    rclcpp::spin(std::make_shared<MdcBridge>());

    rclcpp::shutdown();
    return 0;
}
