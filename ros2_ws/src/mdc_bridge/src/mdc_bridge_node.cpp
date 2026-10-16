/**
 * mdc_bridge —— ROS2 与充电桩显示屏之间的桥
 *
 *   /mdc_data (10Hz)  ──订阅──►  序列化成 JSON  ──UDP──►  界面程序 (Qt)
 *   /mdc 下 5 个 Service ◄─调用─  解析 JSON 命令  ◄─UDP─  界面程序 (Qt)
 *
 * 设计要点：
 *  1. 只依赖 rclcpp + mdc_serial，不依赖 Qt，也不依赖任何第三方 JSON 库
 *     （协议是扁平的，手写序列化几十行搞定，见 simple_json.h）；
 *  2. 界面程序因此可以零 ROS2 依赖，部署到没装 ROS2 的一体屏上；
 *  3. 用 MultiThreadedExecutor 跑在后台线程，主线程专责 UDP 收发，
 *     避免 service 同步调用与 spin 互相阻塞。
 */

#include <rclcpp/rclcpp.hpp>

#include <mdc_serial/msg/mdc_data.hpp>
#include <mdc_serial/srv/fan_ctrl.hpp>
#include <mdc_serial/srv/led_ctrl.hpp>
#include <mdc_serial/srv/powerio_ctrl.hpp>
#include <mdc_serial/srv/relay_ctrl.hpp>
#include <mdc_serial/srv/wireless_charge.hpp>

#include "mdc_protocol.h"
#include "simple_json.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
        m_cmdPort = declare_parameter<int>("cmd_port", mdcproto::kDefaultCmdPort);
        m_statPort = declare_parameter<int>("stat_port", mdcproto::kDefaultStatPort);
        m_peerHost = declare_parameter<std::string>("peer_host", std::string("127.0.0.1"));
        const int peerPort = declare_parameter<int>("peer_port", mdcproto::kDefaultStatPort);
        const std::string topic = declare_parameter<std::string>("topic", std::string("/mdc_data"));
        m_statPeers = declare_parameter<std::vector<std::string>>("extra_peers",
                                                                  std::vector<std::string>());

        // ---------------------------------------------------------- ROS 侧
        m_sub = create_subscription<mdc_serial::msg::MdcData>(
            topic, rclcpp::QoS(10),
            [this](mdc_serial::msg::MdcData::ConstSharedPtr msg)
            { onData(msg); });

        m_fanClient = create_client<mdc_serial::srv::FanCtrl>("/mdc/fan_ctrl");
        m_powerioClient = create_client<mdc_serial::srv::PowerioCtrl>("/mdc/powerio_ctrl");
        m_relayClient = create_client<mdc_serial::srv::RelayCtrl>("/mdc/relay_ctrl");
        m_wchClient = create_client<mdc_serial::srv::WirelessCharge>("/mdc/wireless_charge");
        m_ledClient = create_client<mdc_serial::srv::LedCtrl>("/mdc/led_ctrl");

        // ---------------------------------------------------------- UDP 侧
        if (!initUdp(peerPort))
        {
            RCLCPP_FATAL(get_logger(), "UDP 初始化失败，桥无法工作");
            rclcpp::shutdown();
            return;
        }

        m_recvThread = std::thread([this]
                                   { recvLoop(); });

        m_hbTimer = create_wall_timer(std::chrono::seconds(1), [this]
                                      { sendHeartbeat(); });

        RCLCPP_INFO(get_logger(), "桥已启动");
        RCLCPP_INFO(get_logger(), "  订阅话题      : %s", topic.c_str());
        RCLCPP_INFO(get_logger(), "  监听控制端口  : UDP %d", m_cmdPort);
        RCLCPP_INFO(get_logger(), "  状态发送目标  : UDP %s:%d", m_peerHost.c_str(), peerPort);
        RCLCPP_INFO(get_logger(), "  状态接收端口  : 界面程序需监听 UDP %d", m_statPort);
    }

    ~MdcBridge() override
    {
        m_stop = true;
        if (m_sock >= 0)
        {
            ::shutdown(m_sock, SHUT_RDWR); // 唤醒阻塞中的 recvfrom
            ::close(m_sock);
            m_sock = -1;
        }
        if (m_recvThread.joinable())
            m_recvThread.join();
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

            // ---- 电池 ----
            .num(mdcproto::kBatVoltage, m.battery_total_voltage)
            .num(mdcproto::kBatChargeCurrent, m.battery_charge_current)
            .num(mdcproto::kBatDischargeCurrent, m.battery_discharge_current)
            .num(mdcproto::kBatSoc, m.battery_soc)
            .num(mdcproto::kBatTemp, m.battery_temp)

            // ---- 电源管理 ----
            .num(mdcproto::kPmcTotalVoltage, m.pmc_total_voltage)
            .num(mdcproto::kPmcTotalCurrent, m.pmc_total_current)
            .num(mdcproto::kPmcHeadVoltage, m.pmc_head_voltage)
            .num(mdcproto::kPmcHeadCurrent, m.pmc_head_current)
            .num(mdcproto::kPmcRightVoltage, m.pmc_right_voltage)
            .num(mdcproto::kPmcRightCurrent, m.pmc_right_current)
            .num(mdcproto::kPmcLeftVoltage, m.pmc_left_voltage)
            .num(mdcproto::kPmcLeftCurrent, m.pmc_left_current)
            .num(mdcproto::kPmcNonisolatedVoltage, m.pmc_nonisolated_voltage)
            .num(mdcproto::kPmcNonisolatedCurrent, m.pmc_nonisolated_current)
            .num(mdcproto::kPmcIsolatedCurrent, m.pmc_isolated_current)
            .num(mdcproto::kPmcAlarmStatus, m.pmc_alarm_status)
            .num(mdcproto::kPmcEmergencyStatus, m.pmc_emergency_status)

            // ---- 电源 IO ----
            .num(mdcproto::kIoVo1, m.powerio_vo1)
            .num(mdcproto::kIoVo2, m.powerio_vo2)
            .num(mdcproto::kIoVo3, m.powerio_vo3)
            .num(mdcproto::kIoVo4, m.powerio_vo4)
            .num(mdcproto::kIoSw1, m.powerio_sw1)
            .num(mdcproto::kIoSw2, m.powerio_sw2)
            .num(mdcproto::kIoRelay1, m.powerio_relay1)
            .num(mdcproto::kIoRelay2, m.powerio_relay2)
            .num(mdcproto::kIoEcho1, m.powerio_echo1)
            .num(mdcproto::kIoSeated, m.robot_seated)

            // ---- 红外 ----
            .num(mdcproto::kIrBs, m.ir_bs)
            .num(mdcproto::kIrLA, m.ir_l_a)
            .num(mdcproto::kIrLB, m.ir_l_b)
            .num(mdcproto::kIrRA, m.ir_r_a)
            .num(mdcproto::kIrRB, m.ir_r_b)
            .num(mdcproto::kIrSh, m.ir_sh)

            // ---- 风扇 ----
            .num(mdcproto::kFan1, m.fan1_pwm)
            .num(mdcproto::kFan2, m.fan2_pwm)
            .num(mdcproto::kFan3, m.fan3_pwm)
            .num(mdcproto::kFan4, m.fan4_pwm)
            .num(mdcproto::kFanTemp, m.fan_temp)

            // ---- LED ----
            .arr(mdcproto::kLedCh1, m.led_ch1_data.data(), m.led_ch1_data.size())
            .arr(mdcproto::kLedCh2, m.led_ch2_data.data(), m.led_ch2_data.size())
            .arr(mdcproto::kLedCh3, m.led_ch3_data.data(), m.led_ch3_data.size())
            .arr(mdcproto::kLedRgb, m.led_rgb_data.data(), m.led_rgb_data.size())
            .num(mdcproto::kLedBrightness, m.led_brightness)

            // ---- 无线充电 ----
            .num(mdcproto::kWchRequestFlag, m.wch_request_flag)
            .num(mdcproto::kWchBusy, m.wch_busy)
            .num(mdcproto::kWchResult, m.wch_result)
            .num(mdcproto::kWchReserved, m.wch_reserved)
            .arr(mdcproto::kWchTxData, m.wch_tx_data.data(), m.wch_tx_data.size())
            .arr(mdcproto::kWchRxData, m.wch_rx_data.data(), m.wch_rx_data.size())
            .num(mdcproto::kWchUpdateTick, m.wch_update_tick);

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

    // ================================================================ 下行
    void handleCommand(const std::string &js, const sockaddr_in &from)
    {
        const long id = sjson::getIntOr(js, mdcproto::kId, 0);

        std::string cmd;
        if (!sjson::getString(js, mdcproto::kCmd, cmd))
        {
            sendAck(id, "?", false, "缺少 cmd 字段", from);
            return;
        }

        RCLCPP_INFO(get_logger(), "收到控制指令 #%ld: %s", id, cmd.c_str());

        if (cmd == mdcproto::kCmdRelay)
        {
            auto req = std::make_shared<mdc_serial::srv::RelayCtrl::Request>();
            req->relay1 = static_cast<uint8_t>(sjson::getIntOr(js, "relay1", 0));
            req->relay2 = static_cast<uint8_t>(sjson::getIntOr(js, "relay2", 0));
            callService<mdc_serial::srv::RelayCtrl>(m_relayClient, cmd, id, req, from);
        }
        else if (cmd == mdcproto::kCmdFan)
        {
            auto req = std::make_shared<mdc_serial::srv::FanCtrl::Request>();
            req->fan1_pwm = static_cast<uint8_t>(sjson::getIntOr(js, "fan1", 0));
            req->fan2_pwm = static_cast<uint8_t>(sjson::getIntOr(js, "fan2", 0));
            req->fan3_pwm = static_cast<uint8_t>(sjson::getIntOr(js, "fan3", 0));
            req->fan4_pwm = static_cast<uint8_t>(sjson::getIntOr(js, "fan4", 0));
            callService<mdc_serial::srv::FanCtrl>(m_fanClient, cmd, id, req, from);
        }
        else if (cmd == mdcproto::kCmdPowerio)
        {
            auto req = std::make_shared<mdc_serial::srv::PowerioCtrl::Request>();
            // 注意：位掩码 1=关 0=开（以接口指南为准）
            req->ctrl = static_cast<uint8_t>(sjson::getIntOr(js, "ctrl", 0x0F));
            callService<mdc_serial::srv::PowerioCtrl>(m_powerioClient, cmd, id, req, from);
        }
        else if (cmd == mdcproto::kCmdWch)
        {
            std::vector<long> payload;
            if (!sjson::getIntArray(js, "payload", payload, 4))
            {
                sendAck(id, cmd, false, "wireless_charge 需要 4 字节 payload 数组", from);
                return;
            }
            auto req = std::make_shared<mdc_serial::srv::WirelessCharge::Request>();
            for (size_t i = 0; i < 4; ++i)
                req->payload[i] = static_cast<uint8_t>(payload[i]);
            callService<mdc_serial::srv::WirelessCharge>(m_wchClient, cmd, id, req, from);
        }
        else if (cmd == mdcproto::kCmdLed)
        {
            auto req = std::make_shared<mdc_serial::srv::LedCtrl::Request>();
            req->ch1_mode = static_cast<uint8_t>(sjson::getIntOr(js, "ch1_mode", 0));
            req->ch1_duty = static_cast<uint8_t>(sjson::getIntOr(js, "ch1_duty", 0));
            req->ch2_mode = static_cast<uint8_t>(sjson::getIntOr(js, "ch2_mode", 0));
            req->ch2_duty = static_cast<uint8_t>(sjson::getIntOr(js, "ch2_duty", 0));
            req->ch3_mode = static_cast<uint8_t>(sjson::getIntOr(js, "ch3_mode", 0));
            req->ch3_duty = static_cast<uint8_t>(sjson::getIntOr(js, "ch3_duty", 0));
            req->rgb_mode = static_cast<uint8_t>(sjson::getIntOr(js, "rgb_mode", 0));
            req->r_duty = static_cast<uint8_t>(sjson::getIntOr(js, "r_duty", 0));
            req->g_duty = static_cast<uint8_t>(sjson::getIntOr(js, "g_duty", 0));
            req->b_duty = static_cast<uint8_t>(sjson::getIntOr(js, "b_duty", 0));
            callService<mdc_serial::srv::LedCtrl>(m_ledClient, cmd, id, req, from);
        }
        else
        {
            sendAck(id, cmd, false, "未知指令: " + cmd, from);
            RCLCPP_WARN(get_logger(), "未知指令: %s", cmd.c_str());
        }
    }

    /**
     * 异步调用 service，结果回来后通过 UDP 回 ack。
     * 必须用 async —— 在 UDP 线程里做同步调用会因为没有 spin 而卡死。
     */
    template <typename SrvT>
    void callService(typename rclcpp::Client<SrvT>::SharedPtr client,
                     const std::string &cmd, long id,
                     std::shared_ptr<typename SrvT::Request> req,
                     const sockaddr_in &to)
    {
        if (!client->service_is_ready())
        {
            sendAck(id, cmd, false,
                    std::string("服务不可用: ") + client->get_service_name(), to);
            RCLCPP_WARN(get_logger(), "服务不可用: %s", client->get_service_name());
            return;
        }

        client->async_send_request(
            req, [this, cmd, id, to](typename rclcpp::Client<SrvT>::SharedFuture future)
            {
                const auto res = future.get();
                const std::string msg = res ? res->message : std::string("无响应");
                const bool ok = res && res->success;
                sendAck(id, cmd, ok, msg, to);
                RCLCPP_INFO(get_logger(), "控制指令 #%ld %s -> %s (%s)",
                            id, cmd.c_str(), ok ? "成功" : "失败", msg.c_str()); });
    }

    void sendAck(long id, const std::string &cmd, bool ok,
                 const std::string &msg, const sockaddr_in &to)
    {
        sjson::Builder b;
        b.str(mdcproto::kType, mdcproto::kTypeAck)
            .num(mdcproto::kId, id)
            .str(mdcproto::kCmd, cmd)
            .boolean(mdcproto::kOk, ok)
            .str(mdcproto::kMsg, msg)
            .dbl(mdcproto::kTs, nowSec());

        const std::string payload = b.done();
        ::sendto(m_sock, payload.data(), payload.size(), 0,
                 reinterpret_cast<const sockaddr *>(&to), sizeof(to));
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

        int yes = 1;
        ::setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(m_cmdPort));
        if (::bind(m_sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            RCLCPP_ERROR(get_logger(), "bind(UDP %d) 失败: %s",
                         m_cmdPort, std::strerror(errno));
            return false;
        }

        // 默认对端（界面程序所在地址）。未配置时靠收到的第一个请求自动学习。
        if (!m_peerHost.empty())
        {
            sockaddr_in p{};
            p.sin_family = AF_INET;
            p.sin_port = htons(static_cast<uint16_t>(peerPort));
            if (::inet_pton(AF_INET, m_peerHost.c_str(), &p.sin_addr) == 1)
            {
                std::lock_guard<std::mutex> lk(m_peerMutex);
                m_peer = p;
                m_havePeer = true;
            }
            else
            {
                RCLCPP_WARN(get_logger(), "peer_host 不是合法 IP: %s", m_peerHost.c_str());
            }
        }
        return true;
    }

    void recvLoop()
    {
        std::vector<char> buf(4096);
        while (!m_stop && rclcpp::ok())
        {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            const ssize_t n = ::recvfrom(m_sock, buf.data(), buf.size(), 0,
                                         reinterpret_cast<sockaddr *>(&from), &fromLen);
            if (n <= 0)
            {
                if (errno == EINTR)
                    continue;
                if (m_stop)
                    break;
                continue;
            }

            // 自动学习对端地址：这样跨机部署不用手工配 peer_host
            char ip[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            {
                std::lock_guard<std::mutex> lk(m_peerMutex);
                m_peer = from;
                m_havePeer = true;
            }

            const std::string js(buf.data(), static_cast<size_t>(n));
            RCLCPP_DEBUG(get_logger(), "收到来自 %s 的报文 (%zu 字节)", ip, js.size());
            this->handleCommand(js, from);
        }
    }

    void sendToPeer(const std::string &payload)
    {
        sockaddr_in peer{};
        {
            std::lock_guard<std::mutex> lk(m_peerMutex);
            if (!m_havePeer)
                return; // 还没有对端地址（界面程序还没发过请求）
            peer = m_peer;
        }
        ::sendto(m_sock, payload.data(), payload.size(), 0,
                 reinterpret_cast<const sockaddr *>(&peer), sizeof(peer));
    }

    // ================================================================ 成员
    int m_cmdPort = mdcproto::kDefaultCmdPort;
    int m_statPort = mdcproto::kDefaultStatPort;
    std::string m_peerHost;
    std::vector<std::string> m_statPeers;

    int m_sock = -1;
    sockaddr_in m_peer{};
    bool m_havePeer = false;
    std::mutex m_peerMutex;

    std::thread m_recvThread;
    std::atomic<bool> m_stop{false};
    std::atomic<unsigned long long> m_seq{0};
    std::atomic<int64_t> m_lastDataMs{-100000};
    bool m_lastAlive = false;

    rclcpp::Subscription<mdc_serial::msg::MdcData>::SharedPtr m_sub;
    rclcpp::Client<mdc_serial::srv::FanCtrl>::SharedPtr m_fanClient;
    rclcpp::Client<mdc_serial::srv::PowerioCtrl>::SharedPtr m_powerioClient;
    rclcpp::Client<mdc_serial::srv::RelayCtrl>::SharedPtr m_relayClient;
    rclcpp::Client<mdc_serial::srv::WirelessCharge>::SharedPtr m_wchClient;
    rclcpp::Client<mdc_serial::srv::LedCtrl>::SharedPtr m_ledClient;
    rclcpp::TimerBase::SharedPtr m_hbTimer;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<MdcBridge>();

    // 多线程 executor 跑在后台，主线程留给 UDP 循环
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 2);
    executor.add_node(node);
    std::thread spinThread([&executor]
                           { executor.spin(); });

    // 让主线程也参与 spin（负责 service 响应回调的负载均衡），
    // 同时保证 SIGINT 时能正常退出
    while (rclcpp::ok())
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    executor.cancel();
    if (spinThread.joinable())
        spinThread.join();

    node.reset();
    rclcpp::shutdown();
    return 0;
}
