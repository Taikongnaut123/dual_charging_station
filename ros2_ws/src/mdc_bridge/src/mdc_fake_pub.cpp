/**
 * mdc_fake_pub —— 假设备，用于在没有真机时验证「桥 + 界面」整条链路
 *
 * 按 doc/MDC接口使用指南.md 的新协议（0x12 充电桩状态帧）造数据：
 *   · 约 2Hz 发布 /mdc_data（与真机 500ms 一帧一致）
 *   · 模拟「手动充电口充电 → 空闲 → 自动充电口充电 → 空闲」的循环
 *   · CCU 电机状态按真实流程推进：初始化中 → 初始化完成 → 回零 → 到位
 *
 * 注意：新协议是**只收不发**，所以这里不再代管任何 Service ——
 * 旧版的 5 个假服务（fan/relay/powerio/wch/led）随下行链路一起删除了。
 *
 * 用法： ros2 run mdc_bridge mdc_fake_pub
 */

#include <rclcpp/rclcpp.hpp>

#include <mdc_serial/msg/mdc_data.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>

namespace
{

    constexpr double kCycle = 140.0; // 一个完整循环（秒）

    // 时间轴
    constexpr double kManStart = 20.0;     // 手动口开始充电
    constexpr double kManEnd = 65.0;       // 手动口充电结束（充满）
    constexpr double kManFullEnd = 75.0;   // 手动口"充满"状态维持到这时
    constexpr double kAutoStart = 80.0;    // 自动口开始充电
    constexpr double kAutoEnd = 125.0;     // 自动口充电结束
    constexpr double kAutoFullEnd = 135.0; // 自动口"充满"维持到这时

    constexpr double kSocStart = 38.0;
    constexpr double kMaxChargeDeciA = 150.0; // 15.0A（协议单位 0.1A）

    // 空闲时两个口的静置电压对应 SOC
    constexpr double kManualIdleSoc = 62.0;
    constexpr double kAutoIdleSoc = 45.0;

    /** 充电中：SOC 线性爬升；电流前 80% 恒流，后 20% 线性回落 */
    void chargeProfile(double p, double *soc, double *deciA)
    {
        *soc = kSocStart + (100.0 - kSocStart) * p;
        if (p < 0.8)
            *deciA = kMaxChargeDeciA;
        else
            *deciA = kMaxChargeDeciA - (kMaxChargeDeciA - 20.0) * ((p - 0.8) / 0.2);
    }

    /** 48V 电池组：电压随 SOC 上升 */
    uint16_t packVoltage(double soc)
    {
        return static_cast<uint16_t>(46000.0 + 80.0 * soc);
    }

} // namespace

class FakeDevice : public rclcpp::Node
{
public:
    FakeDevice()
        : rclcpp::Node("mdc_fake_pub")
    {
        // 与桥一致用 BEST_EFFORT，避免订阅端 QoS 不匹配
        m_pub = create_publisher<mdc_serial::msg::MdcData>("/mdc_data",
                                                           rclcpp::SensorDataQoS());
        m_t0 = now();
        m_timer = create_wall_timer(std::chrono::milliseconds(500), [this]
                                    { tick(); });

        // 调试/演示用：强制指定两个口的 link_status，用来验证界面置灰逻辑。
        //   -1 跟随充电状态（默认）
        //    0 都不启用        1 仅手动
        //    2 仅自动          3 都启用
        m_linkOverride = static_cast<int>(declare_parameter<int>("link_override", -1));

        RCLCPP_INFO(get_logger(), "假设备已启动：约 2Hz 发布 /mdc_data（周期 %.0f 秒）", kCycle);
        RCLCPP_INFO(get_logger(), "模拟流程：就位 -> 手动口充电 -> 空闲 -> 自动口充电 -> 空闲");
        RCLCPP_INFO(get_logger(), "注意：新协议只收不发，本节点不提供任何控制 Service");
    }

private:
    void tick()
    {
        const double t = std::fmod((now() - m_t0).seconds(), kCycle);

        const bool manCharging = (t >= kManStart && t < kManEnd);
        const bool autoCharging = (t >= kAutoStart && t < kAutoEnd);
        const bool manFull = (t >= kManEnd && t < kManFullEnd);
        const bool autoFull = (t >= kAutoEnd && t < kAutoFullEnd);

        mdc_serial::msg::MdcData m;
        m.header.stamp = now();
        m.header.frame_id = "charge_station";

        // ------------------------------------------------ 手动充电口
        double manSoc = kManualIdleSoc;
        double manDeciA = 0.0;
        if (manCharging)
            chargeProfile((t - kManStart) / (kManEnd - kManStart), &manSoc, &manDeciA);
        else if (manFull)
            manSoc = 100.0;

        m.manual_chg_switch = (manCharging || manFull) ? 1 : 0;
        m.manual_battery_id = 1;
        m.manual_total_voltage = packVoltage(manSoc);
        m.manual_charge_current = static_cast<uint16_t>(manDeciA);
        m.manual_discharge_current = 0;
        m.manual_soc = static_cast<uint16_t>(manSoc + 0.5);
        m.manual_temp = static_cast<uint16_t>(26.0 + (manCharging ? 8.0 : 1.0) +
                                              1.5 * std::sin(t * 0.4));

        // ------------------------------------------------ 自动充电口
        double autoSoc = kAutoIdleSoc;
        double autoDeciA = 0.0;
        if (autoCharging)
            chargeProfile((t - kAutoStart) / (kAutoEnd - kAutoStart), &autoSoc, &autoDeciA);
        else if (autoFull)
            autoSoc = 100.0;

        m.auto_chg_switch = (autoCharging || autoFull) ? 1 : 0;
        m.auto_battery_id = 2;
        m.auto_total_voltage = packVoltage(autoSoc);
        m.auto_charge_current = static_cast<uint16_t>(autoDeciA);
        m.auto_discharge_current = 0;
        m.auto_soc = static_cast<uint16_t>(autoSoc + 0.5);
        m.auto_temp = static_cast<uint16_t>(26.0 + (autoCharging ? 8.0 : 1.0) +
                                            1.5 * std::sin(t * 0.4 + 2.0));

        // ------------------------------------------------ 充电桩状态
        // CCU 电机：初始化中 -> 初始化完成 -> 回零中 -> 回零完成 -> 到位中 -> 到位完成
        if (t < 3.0)
            m.ccu_motor_status = 1;
        else if (t < 8.0)
            m.ccu_motor_status = 2;
        else if (t < 12.0)
            m.ccu_motor_status = 3;
        else if (t < 16.0)
            m.ccu_motor_status = 4;
        else if (t < 18.0)
            m.ccu_motor_status = 5;
        else
            m.ccu_motor_status = 6;

        if (manCharging || manFull)
            m.charge_mode = 1; // 手动
        else if (autoCharging || autoFull)
            m.charge_mode = 2; // 自动
        else
            m.charge_mode = 0; // 待机

        if (manCharging || autoCharging)
            m.charge_state = 2; // 充电中
        else if (manFull || autoFull)
            m.charge_state = 3; // 充满
        else
            m.charge_state = 1; // 空闲

        m.fan_status = (manCharging || autoCharging || manFull || autoFull) ? 1 : 0;
        m.hall_switch = (t >= 18.0) ? 1 : 0; // 车到位后霍尔触发

        // 两个口的"已连接"标志。真机由硬件给，这里默认跟随充电状态，
        // 需要单独验证界面时用 link_override 参数强制指定。
        bool manLink = (manCharging || manFull);
        bool autoLink = (autoCharging || autoFull);
        switch (m_linkOverride)
        {
        case 0: manLink = false; autoLink = false; break;
        case 1: manLink = true;  autoLink = false; break;
        case 2: manLink = false; autoLink = true;  break;
        case 3: manLink = true;  autoLink = true;  break;
        default: break;
        }

        m.manual_link_status = manLink ? 1 : 0;
        m.manual_charge_status = manCharging ? 1 : 0;
        m.auto_link_status = autoLink ? 1 : 0;
        m.auto_charge_status = autoCharging ? 1 : 0;

        m_pub->publish(m);
    }

    rclcpp::Publisher<mdc_serial::msg::MdcData>::SharedPtr m_pub;
    rclcpp::TimerBase::SharedPtr m_timer;
    rclcpp::Time m_t0;
    int m_linkOverride = -1;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<FakeDevice>());
    rclcpp::shutdown();
    return 0;
}
