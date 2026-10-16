/**
 * mdc_fake_pub —— 假设备，用于在没有真机时验证「桥 + 界面」整条链路
 *
 * 以 10Hz 发布 /mdc_data，按一个 160 秒的周期模拟完整充电流程：
 *   0-5s   无车
 *   5-8s   停车检测中（红外 A/B 管依次触发）
 *   8-10s  停车握手成功 (ir_sh=1)
 *   10-12s 机器人落座 (robot_seated=1)
 *   12-132s 充电中（38% -> 100%，恒流->恒压回落的电流曲线）
 *   132-142s 充电完成（电流归零、继电器断开）
 *   142-160s 机器人离桩
 *
 * 用法： ros2 run mdc_bridge mdc_fake_pub
 */

#include <rclcpp/rclcpp.hpp>

#include <mdc_serial/msg/mdc_data.hpp>
#include <mdc_serial/srv/fan_ctrl.hpp>
#include <mdc_serial/srv/led_ctrl.hpp>
#include <mdc_serial/srv/powerio_ctrl.hpp>
#include <mdc_serial/srv/relay_ctrl.hpp>
#include <mdc_serial/srv/wireless_charge.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace
{

    constexpr double kCycle = 160.0; // 周期秒
    constexpr double kSocStart = 38.0;
    constexpr double kSocCvStart = 80.0;
    constexpr double kMaxChargeMa = 30000.0;

    const char *kAckMsg = "假节点已接受指令（未真正驱动硬件）";

} // namespace

class FakeDevice : public rclcpp::Node
{
public:
    FakeDevice()
        : rclcpp::Node("mdc_fake_pub")
    {
        m_pub = create_publisher<mdc_serial::msg::MdcData>("/mdc_data", rclcpp::QoS(10));
        m_t0 = now();
        m_timer = create_wall_timer(std::chrono::milliseconds(100), [this]
                                    { tick(); });
        createServices();

        RCLCPP_INFO(get_logger(), "假设备已启动：10Hz 发布 /mdc_data（周期 %.0f 秒）", kCycle);
        RCLCPP_INFO(get_logger(), "并代管 5 个服务：fan_ctrl / powerio_ctrl / relay_ctrl / "
                                  "wireless_charge / led_ctrl");
    }

private:
    // ============================================================ 假服务
    // 收到指令后只做一件事：把指令值记下来并在下帧上报。
    // 目的是让控制链路（界面 -> UDP -> 桥 -> service -> 应答 -> 界面）能被端到端验证。
    void createServices()
    {
        m_fanSrv = create_service<mdc_serial::srv::FanCtrl>(
            "/mdc/fan_ctrl",
            [this](const std::shared_ptr<mdc_serial::srv::FanCtrl::Request> req,
                   std::shared_ptr<mdc_serial::srv::FanCtrl::Response> res)
            {
                m_fan[0] = req->fan1_pwm;
                m_fan[1] = req->fan2_pwm;
                m_fan[2] = req->fan3_pwm;
                m_fan[3] = req->fan4_pwm;
                m_fanOverridden = true;
                RCLCPP_INFO(get_logger(), "[假服务] fan_ctrl -> %u/%u/%u/%u",
                            req->fan1_pwm, req->fan2_pwm, req->fan3_pwm, req->fan4_pwm);
                res->success = true;
                res->message = kAckMsg;
            });

        m_powerioSrv = create_service<mdc_serial::srv::PowerioCtrl>(
            "/mdc/powerio_ctrl",
            [this](const std::shared_ptr<mdc_serial::srv::PowerioCtrl::Request> req,
                   std::shared_ptr<mdc_serial::srv::PowerioCtrl::Response> res)
            {
                m_voMask = req->ctrl;
                RCLCPP_INFO(get_logger(), "[假服务] powerio_ctrl -> mask=0x%02X", req->ctrl);
                res->success = true;
                res->message = kAckMsg;
            });

        m_relaySrv = create_service<mdc_serial::srv::RelayCtrl>(
            "/mdc/relay_ctrl",
            [this](const std::shared_ptr<mdc_serial::srv::RelayCtrl::Request> req,
                   std::shared_ptr<mdc_serial::srv::RelayCtrl::Response> res)
            {
                m_relay[0] = req->relay1;
                m_relay[1] = req->relay2;
                m_relayOverridden = true;
                RCLCPP_INFO(get_logger(), "[假服务] relay_ctrl -> %u/%u",
                            req->relay1, req->relay2);
                res->success = true;
                res->message = kAckMsg;
            });

        m_wchSrv = create_service<mdc_serial::srv::WirelessCharge>(
            "/mdc/wireless_charge",
            [this](const std::shared_ptr<mdc_serial::srv::WirelessCharge::Request> req,
                   std::shared_ptr<mdc_serial::srv::WirelessCharge::Response> res)
            {
                for (size_t i = 0; i < 4; ++i)
                    m_tx[i] = req->payload[i];
                m_wchResult = 1;       // 处理中
                m_wchIssuedAt = now(); // 1.5s 后置为成功，模拟无线端应答
                RCLCPP_INFO(get_logger(), "[假服务] wireless_charge -> %02X %02X %02X %02X",
                            m_tx[0], m_tx[1], m_tx[2], m_tx[3]);
                res->success = true;
                res->message = kAckMsg;
            });

        m_ledSrv = create_service<mdc_serial::srv::LedCtrl>(
            "/mdc/led_ctrl",
            [this](const std::shared_ptr<mdc_serial::srv::LedCtrl::Request> req,
                   std::shared_ptr<mdc_serial::srv::LedCtrl::Response> res)
            {
                m_ledMode[0] = req->ch1_mode;
                m_ledDuty[0] = req->ch1_duty;
                m_ledMode[1] = req->ch2_mode;
                m_ledDuty[1] = req->ch2_duty;
                m_ledMode[2] = req->ch3_mode;
                m_ledDuty[2] = req->ch3_duty;
                m_ledBrightness = req->ch1_duty;
                m_rgb[0] = req->r_duty;
                m_rgb[1] = req->g_duty;
                m_rgb[2] = req->b_duty;
                m_ledOverridden = true;
                RCLCPP_INFO(get_logger(),
                            "[假服务] led_ctrl -> ch1=%u/%u ch2=%u/%u ch3=%u/%u rgb=%u,%u,%u",
                            req->ch1_mode, req->ch1_duty, req->ch2_mode, req->ch2_duty,
                            req->ch3_mode, req->ch3_duty,
                            req->r_duty, req->g_duty, req->b_duty);
                res->success = true;
                res->message = kAckMsg;
            });
    }

    // ============================================================ 周期上报
    void tick()
    {
        const double t = std::fmod((now() - m_t0).seconds(), kCycle);

        mdc_serial::msg::MdcData m{};

        // ---------------------------------------------------- 阶段判定
        const bool detecting = (t >= 5.0 && t < 8.0);
        const bool handshaked = (t >= 8.0 && t < 142.0);
        const bool seated = (t >= 10.0 && t < 142.0);
        const bool charging = (t >= 12.0 && t < 132.0);
        const bool finished = (t >= 132.0 && t < 142.0);

        // ---------------------------------------------------- 电池
        double soc = kSocStart;
        double chargeMa = 0.0;
        double dischargeMa = 0.0;

        if (charging)
        {
            // 充电进度：恒流段占前 60% 时长，之后是恒压回落段
            const double span = 132.0 - 12.0;
            const double p = (t - 12.0) / span;
            const double socCv = (kSocCvStart - kSocStart) / (100.0 - kSocStart);
            if (p < socCv)
            {
                soc = kSocStart + (kSocCvStart - kSocStart) * (p / socCv);
                chargeMa = kMaxChargeMa;
            }
            else
            {
                const double q = (p - socCv) / (1.0 - socCv);
                soc = kSocCvStart + (100.0 - kSocCvStart) * q;
                chargeMa = kMaxChargeMa * (1.0 - 0.92 * q);
            }
        }
        else if (t >= 132.0)
        {
            soc = 100.0;
        }
        else
        {
            // 无车时电量缓慢自耗
            soc = std::max(20.0, kSocStart - 0.05 * t);
            dischargeMa = detecting ? 0.0 : 3200.0;
        }

        m.battery_soc = static_cast<int32_t>(soc + 0.5);
        m.battery_charge_current = static_cast<int32_t>(chargeMa);
        m.battery_discharge_current = static_cast<int32_t>(dischargeMa);
        // 16S 磷酸铁锂：空 40.0V 满 58.4V，再叠加内阻压降
        m.battery_total_voltage =
            static_cast<int32_t>(40000.0 + 184.0 * m.battery_soc + chargeMa * 0.03);
        m.battery_temp = static_cast<int32_t>(26.0 + (charging ? 6.0 : 1.0) +
                                              1.5 * std::sin(t * 0.05));

        // ---------------------------------------------------- 电源管理 PMC
        const double totalMa = chargeMa + (detecting ? 1200.0 : 600.0);
        m.pmc_total_voltage = static_cast<int32_t>(48000.0 + 200.0 * std::sin(t * 0.7));
        m.pmc_total_current = static_cast<int32_t>(totalMa);
        m.pmc_head_voltage = static_cast<int32_t>(48000.0 + 150.0 * std::sin(t * 1.3));
        m.pmc_head_current = static_cast<int32_t>((detecting || !seated) ? 820.0 : 180.0);
        m.pmc_right_voltage = static_cast<int32_t>(47900.0 + 160.0 * std::sin(t * 1.1 + 1.0));
        m.pmc_right_current = static_cast<int32_t>((detecting || !seated) ? 1450.0 : 220.0);
        m.pmc_left_voltage = static_cast<int32_t>(48050.0 + 140.0 * std::sin(t * 0.9 + 2.0));
        m.pmc_left_current = static_cast<int32_t>((detecting || !seated) ? 1390.0 : 210.0);
        m.pmc_nonisolated_voltage = static_cast<int32_t>(12000.0 + 30.0 * std::sin(t * 2.0));
        m.pmc_nonisolated_current =
            static_cast<int32_t>(900.0 + (charging ? 700.0 : 0.0) + 60.0 * std::sin(t * 0.8));
        m.pmc_isolated_current = static_cast<int32_t>(320.0 + 40.0 * std::sin(t * 1.7));
        m.pmc_alarm_status = 0;
        m.pmc_emergency_status = 0;

        // ---------------------------------------------------- 电源 IO（可被指令覆盖）
        m.powerio_vo1 = static_cast<uint8_t>((m_voMask >> 0) & 1u);
        m.powerio_vo2 = static_cast<uint8_t>((m_voMask >> 1) & 1u);
        m.powerio_vo3 = static_cast<uint8_t>((m_voMask >> 2) & 1u);
        m.powerio_vo4 = static_cast<uint8_t>((m_voMask >> 3) & 1u);
        m.powerio_sw1 = 1;
        m.powerio_sw2 = 0;
        if (m_relayOverridden)
        {
            m.powerio_relay1 = m_relay[0];
            m.powerio_relay2 = m_relay[1];
        }
        else
        {
            m.powerio_relay1 = charging ? 1 : 0;
            m.powerio_relay2 = charging ? 1 : 0;
        }
        m.powerio_echo1 = seated ? 1 : 0;
        m.robot_seated = seated ? 1 : 0;

        // ---------------------------------------------------- 红外
        m.ir_bs = charging ? 1 : 0;
        m.ir_l_a = (detecting || handshaked) ? 1 : 0;
        m.ir_l_b = (handshaked) ? 1 : 0;
        m.ir_r_a = (t >= 6.0 && t < 142.0) ? 1 : 0;
        m.ir_r_b = (handshaked) ? 1 : 0;
        m.ir_sh = handshaked ? 1 : 0;

        // ---------------------------------------------------- 风扇（可被指令覆盖）
        m.fan1_pwm = m_fan[0];
        m.fan2_pwm = m_fan[1];
        m.fan3_pwm = m_fan[2];
        m.fan4_pwm = m_fan[3];
        if (!m_fanOverridden)
        {
            const int fanTarget = charging ? 80 : (finished ? 40 : 0);
            m.fan1_pwm = static_cast<uint8_t>(fanTarget);
            m.fan2_pwm = static_cast<uint8_t>(fanTarget);
            m.fan3_pwm = static_cast<uint8_t>(fanTarget / 2);
            m.fan4_pwm = static_cast<uint8_t>(fanTarget / 4);
        }
        m.fan_temp = static_cast<int32_t>(m.battery_temp + 4);

        // ---------------------------------------------------- LED（可被指令覆盖）
        if (m_ledOverridden)
        {
            for (size_t i = 0; i < 8; ++i)
            {
                m.led_ch1_data[i] = static_cast<uint8_t>(i * 100 / 8 < unsigned(m_ledDuty[0]) ? 255 : 0);
                m.led_ch2_data[i] = static_cast<uint8_t>(i * 100 / 8 < unsigned(m_ledDuty[1]) ? 255 : 0);
                m.led_ch3_data[i] = static_cast<uint8_t>(i * 100 / 8 < unsigned(m_ledDuty[2]) ? 255 : 0);
            }
            m.led_rgb_data[0] = static_cast<uint8_t>(m_rgb[0] * 255 / 100);
            m.led_rgb_data[1] = static_cast<uint8_t>(m_rgb[1] * 255 / 100);
            m.led_rgb_data[2] = static_cast<uint8_t>(m_rgb[2] * 255 / 100);
            for (size_t i = 3; i < m.led_rgb_data.size(); ++i)
                m.led_rgb_data[i] = 0;
            m.led_brightness = m_ledBrightness;
        }
        else
        {
            const int level = static_cast<int>(m.battery_soc) * 8 / 100;
            for (size_t i = 0; i < m.led_ch1_data.size(); ++i)
                m.led_ch1_data[i] = static_cast<uint8_t>(i < static_cast<size_t>(level) ? 255 : 0);
            for (size_t i = 0; i < m.led_ch2_data.size(); ++i)
                m.led_ch2_data[i] = static_cast<uint8_t>(charging ? 255 : 0);
            for (size_t i = 0; i < m.led_ch3_data.size(); ++i)
                m.led_ch3_data[i] = static_cast<uint8_t>(handshaked ? 180 : 0);

            m.led_rgb_data[0] = 0;
            m.led_rgb_data[1] = static_cast<uint8_t>(charging ? 200 : 0);
            m.led_rgb_data[2] = static_cast<uint8_t>(charging ? 60 : 0);
            m.led_rgb_data[3] = static_cast<uint8_t>(charging ? 100 : 30);
            for (size_t i = 4; i < m.led_rgb_data.size(); ++i)
                m.led_rgb_data[i] = 0;
            m.led_brightness = 60;
        }

        // ---------------------------------------------------- 无线充电
        if (m_wchResult == 1 && (now() - m_wchIssuedAt).seconds() > 1.5)
            m_wchResult = 2; // 处理中 -> 成功，模拟无线端应答

        m.wch_request_flag = charging ? 1 : 0;
        m.wch_busy = (m_wchResult == 1) ? 1 : 0;
        if (m_wchResult != 0)
        {
            m.wch_result = m_wchResult;
        }
        else
        {
            const int beat = static_cast<int>(t) % 6;
            m.wch_result = static_cast<uint8_t>(charging ? (beat < 2 ? 1 : 2) : 0);
        }
        m.wch_reserved = 0;
        for (size_t i = 0; i < 4; ++i)
            m.wch_tx_data[i] = m_tx[i];
        m.wch_rx_data[0] = 0x02;
        m.wch_rx_data[1] = static_cast<uint8_t>(m.battery_soc);
        m.wch_rx_data[2] = 0x00;
        m.wch_rx_data[3] = static_cast<uint8_t>(charging ? 0x01 : 0x00);
        m.wch_update_tick = static_cast<uint32_t>(t * 1000.0) % 1000000;

        m_pub->publish(m);
    }

    rclcpp::Publisher<mdc_serial::msg::MdcData>::SharedPtr m_pub;
    rclcpp::TimerBase::SharedPtr m_timer;
    rclcpp::Time m_t0;

    rclcpp::Service<mdc_serial::srv::FanCtrl>::SharedPtr m_fanSrv;
    rclcpp::Service<mdc_serial::srv::PowerioCtrl>::SharedPtr m_powerioSrv;
    rclcpp::Service<mdc_serial::srv::RelayCtrl>::SharedPtr m_relaySrv;
    rclcpp::Service<mdc_serial::srv::WirelessCharge>::SharedPtr m_wchSrv;
    rclcpp::Service<mdc_serial::srv::LedCtrl>::SharedPtr m_ledSrv;

    // 被指令改写过的状态（回调都在同一个 executor 线程上，无需加锁）
    uint8_t m_fan[4] = {0, 0, 0, 0};
    bool m_fanOverridden = false;
    uint8_t m_relay[2] = {0, 0};
    bool m_relayOverridden = false;
    uint8_t m_voMask = 0x0C; // bit=1 表示关：默认 VO1/VO2 开，VO3/VO4 关
    uint8_t m_ledMode[3] = {0, 0, 0};
    uint8_t m_ledDuty[3] = {60, 60, 60};
    uint8_t m_ledBrightness = 60;
    uint8_t m_rgb[3] = {0, 0, 0};
    bool m_ledOverridden = false;
    uint8_t m_tx[4] = {1, 1, 1, 1};
    uint8_t m_wchResult = 0;
    rclcpp::Time m_wchIssuedAt{0, 0, RCL_ROS_TIME};
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<FakeDevice>());
    rclcpp::shutdown();
    return 0;
}
