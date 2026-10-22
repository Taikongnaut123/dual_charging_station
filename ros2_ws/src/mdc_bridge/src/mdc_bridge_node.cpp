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
#include "run_log_dir.h"
#include "simple_json.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
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
        m_topic = declare_parameter<std::string>("topic", std::string("/mdc_data"));
        const std::string &topic = m_topic;

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

        // 5 秒一次的体检日志：发布者数量 / 累计帧数 / 发送失败数。
        // DEBUG 级，平时不输出；排查时加 --ros-args --log-level debug 打开。
        m_dbgTimer = create_wall_timer(std::chrono::seconds(5), [this]
                                       { onDebugTick(); });

        // "多久没有发布者"从启动就开始计，否则要等第一个 tick 才起步，
        // 实际报警会比预期晚一整个周期。
        m_noPubSinceMs = steadyMs();

        RCLCPP_INFO(get_logger(), "桥已启动（单向：只把设备数据转发给界面）");
        RCLCPP_INFO(get_logger(), "  订阅话题      : %s", topic.c_str());
        RCLCPP_INFO(get_logger(), "  状态发送目标  : UDP %s:%d", m_peerHost.c_str(), peerPort);

        // 现场最常见的问题是 ROS_DOMAIN_ID 不一致：两个节点在同一台机器上也会
        // 互相看不见，而且不报任何错。把实际生效的域打出来，省得靠猜。
        const char *domainId = std::getenv("ROS_DOMAIN_ID");
        const char *rmw = std::getenv("RMW_IMPLEMENTATION");
        RCLCPP_INFO(get_logger(), "  ROS_DOMAIN_ID : %s",
                    (domainId && *domainId)
                        ? domainId
                        : "0（未设置。真机是 122，对不上就一帧都收不到）");
        RCLCPP_INFO(get_logger(), "  RMW           : %s",
                    (rmw && *rmw) ? rmw : "默认（rmw_fastrtps_cpp）");

        // 日志落到哪里直接说出来，现场不用猜 $HOME 是什么
        const char *logDir = std::getenv("ROS_LOG_DIR");
        RCLCPP_INFO(get_logger(), "  日志目录      : %s",
                    (logDir && *logDir) ? logDir : "默认（$HOME/.ros/log）");
        RCLCPP_INFO(get_logger(), "  QoS           : SensorDataQoS（BEST_EFFORT，兼容 RELIABLE 发布者）");
        RCLCPP_INFO(get_logger(),
                    "提示：想看每帧细节和 5 秒一次统计，加 --ros-args --log-level debug");
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
        ++m_framesReceived;
        m_lastDataMs = steadyMs();

        const std::string payload = buildState(*m);

        // 第一帧是个关键里程碑：有它说明 ROS 这一跳通了，之后的麻烦都在 UDP 上。
        if (!m_firstFrameSeen)
        {
            m_firstFrameSeen = true;
            RCLCPP_INFO(get_logger(),
                        "收到第一帧 %s（模式=%u 状态=%u 手动SOC=%u%% 自动SOC=%u%%，JSON %zu 字节）",
                        m_topic.c_str(), unsigned(m->charge_mode), unsigned(m->charge_state),
                        unsigned(m->manual_soc), unsigned(m->auto_soc), payload.size());
        }

        RCLCPP_DEBUG(get_logger(),
                     "转发 seq=%llu 模式=%u 状态=%u | 手动 SOC=%u%% %umV %u×0.1A 温度%u | "
                     "自动 SOC=%u%% %umV %u×0.1A | hall=%d 连接=%d/%d 充电=%d/%d | JSON %zu 字节",
                     m_seq, unsigned(m->charge_mode), unsigned(m->charge_state),
                     unsigned(m->manual_soc), unsigned(m->manual_total_voltage),
                     unsigned(m->manual_charge_current), unsigned(m->manual_temp),
                     unsigned(m->auto_soc), unsigned(m->auto_total_voltage),
                     unsigned(m->auto_charge_current),
                     int(m->hall_switch), int(m->manual_link_status), int(m->auto_link_status),
                     int(m->manual_charge_status), int(m->auto_charge_status),
                     payload.size());

        sendToPeer(payload);
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

        RCLCPP_DEBUG(get_logger(), "发送心跳 ros_alive=1 mdc_alive=%d", mdcAlive ? 1 : 0);

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

        const ssize_t n = ::sendto(m_sock, payload.data(), payload.size(), 0,
                                   reinterpret_cast<const sockaddr *>(&m_peer),
                                   sizeof(m_peer));
        if (n < 0)
        {
            // 界面没起来 / 网络不通时会一直失败，不能每帧刷屏：
            // 错误码换了才提醒，或者累计到 100 次的整数倍再提醒一次
            const int err = errno;
            ++m_sendErrors;
            if (err != m_lastSendErrno || m_sendErrors % 100 == 0)
                RCLCPP_WARN(get_logger(),
                            "发送 UDP 到 %s:%u 失败（累计 %llu 次）: %s",
                            m_peerHost.c_str(), unsigned(ntohs(m_peer.sin_port)),
                            m_sendErrors, std::strerror(err));
            m_lastSendErrno = err;
            return;
        }

        if (m_lastSendErrno != 0)
        {
            RCLCPP_INFO(get_logger(), "UDP 发送恢复正常（此前失败 %llu 次）", m_sendErrors);
            m_lastSendErrno = 0;
        }
        m_bytesSent += static_cast<unsigned long long>(n);
    }

    // ================================================================ 体检
    void onDebugTick()
    {
        const size_t pubCount = m_sub ? m_sub->get_publisher_count() : 0;

        if (pubCount != m_lastPubCount)
        {
            m_lastPubCount = pubCount;
            if (pubCount > 0)
            {
                RCLCPP_INFO(get_logger(), "%s 的发布者数量变为 %zu 个",
                            m_topic.c_str(), pubCount);
                m_warnedNoPublisher = false;
                m_noPubSinceMs = 0;
            }
        }

        // "有话题、但没有任何发布者" 是最容易误判的一种没数据：
        // 域不同 / 话题名不同 / 类型不同都会表现成 pubCount == 0，且不报错。
        if (pubCount == 0)
        {
            if (m_noPubSinceMs == 0)
                m_noPubSinceMs = steadyMs();
            else if (!m_warnedNoPublisher && steadyMs() - m_noPubSinceMs >= 10000)
            {
                m_warnedNoPublisher = true;
                RCLCPP_WARN(get_logger(),
                            "10 秒内没有任何发布者匹配 %s。请确认：ROS_DOMAIN_ID（真机 122）、"
                            "话题名、以及 mdc_node 是否在运行",
                            m_topic.c_str());
            }
        }

        RCLCPP_DEBUG(get_logger(),
                     "统计：发布者 %zu 个，已收 %llu 帧，已发 %llu 字节，发送失败 %llu 次，"
                     "最近一帧距今 %lld ms",
                     pubCount, m_framesReceived, m_bytesSent, m_sendErrors,
                     static_cast<long long>(steadyMs() - m_lastDataMs));
    }

    // ================================================================ 成员
    std::string m_peerHost;
    std::string m_topic;

    int m_sock = -1;
    sockaddr_in m_peer{};
    bool m_havePeer = false;

    unsigned long long m_seq = 0;
    int64_t m_lastDataMs = -100000;
    bool m_lastAlive = false;

    // ---- 调试统计 ----
    unsigned long long m_framesReceived = 0;
    unsigned long long m_bytesSent = 0;
    unsigned long long m_sendErrors = 0;
    int m_lastSendErrno = 0;
    bool m_firstFrameSeen = false;
    size_t m_lastPubCount = 0;
    bool m_warnedNoPublisher = false;
    int64_t m_noPubSinceMs = 0;

    rclcpp::Subscription<mdc_serial::msg::MdcData>::SharedPtr m_sub;
    rclcpp::TimerBase::SharedPtr m_hbTimer;
    rclcpp::TimerBase::SharedPtr m_dbgTimer;
};

int main(int argc, char **argv)
{
    // 必须在 rclcpp::init 之前：日志目录是 init 时定下来的
    runlog::useRunDirLogs();

    rclcpp::init(argc, argv);

    // 没有下行链路了，单线程 spin 足够：订阅回调里只做序列化 + 一次 sendto
    rclcpp::spin(std::make_shared<MdcBridge>());

    rclcpp::shutdown();
    return 0;
}
