#ifndef MDC_PROTOCOL_H
#define MDC_PROTOCOL_H

/**
 * ============================================================================
 *  mdc_bridge  <->  充电桩显示屏程序   之间的 UDP JSON 协议
 * ============================================================================
 *
 *  这是**双方唯一的契约**。桥节点（ros2_ws/src/mdc_bridge）和 Qt 界面程序
 *  （bridgeclient.cpp）都 include 这个文件，所有键名只在这里定义一次，
 *  避免两边各写一份字符串导致对不上。
 *
 *  ── 单向协议：只收不发 ──
 *  按 doc/MDC接口使用指南.md：充电桩 UART 协议是**只收不发**，充电控制由
 *  充电桩主机自主完成，mdc_node 不提供任何下行 Service。因此：
 *
 *      · 桥只往界面发（状态帧 + 心跳），界面**不发送任何东西**
 *      · 没有 cmd / ack / id 这些下行字段
 *      · 界面不需要知道桥的地址，只需要绑定一个端口等数据
 *
 *  ── 上行（桥 → 界面）──
 *    状态帧  约 2Hz：{"t":"s","seq":123,"ts":1729.5, ...21 个字段...}
 *    心跳     1Hz  ：{"t":"hb","ros":1,"mdc":0,"ts":1729.5}
 *                    ros=1 桥进程活着；mdc=1 最近 2s 内收到过设备数据
 *
 *  界面侧的三态判定：
 *    · 连心跳都收不到          -> NoBridge   桥进程未运行 / 网络不通
 *    · 只有心跳、mdc=0         -> BridgeOnly 桥正常，但设备没上报
 *    · 收到状态帧              -> Online     数据正常
 *
 *  ── 单位约定 ──
 *  与 MdcData.msg **完全一致，网络层不做任何换算**：
 *    电压 mV；电流 0.1A（150 表示 15.0A）；温度 ℃；SOC %。
 *  换算只在界面显示时做一次，避免多处换算导致量纲混乱。
 *
 *  ── 为什么键名比 msg 里的短 ──
 *  每包要塞 21 个字段，键名用全名会让包明显变大。用短键后单包约 400 字节，
 *  远小于 MTU。代价是可读性差些，所以每个键都写了注释。
 *
 *  ── Header 为什么没传 ──
 *  MdcData.msg 里的 std_msgs/Header 是 ROS2 的元数据（stamp + frame_id）。
 *  界面只需要"数据什么时候到的"，用封套里的 ts 或本地到达时间即可，
 *  而 frame_id 是固定值 "charge_station"，传过去没有意义，故省略。
 */

namespace mdcproto
{
    // ---------------------------------------------------------------- 包类型
    constexpr const char *kType = "t";
    constexpr const char *kTypeState = "s";      // 桥 -> 界面：状态帧
    constexpr const char *kTypeHeartbeat = "hb"; // 桥 -> 界面：心跳

    // ---------------------------------------------------------------- 通用键
    constexpr const char *kSeq = "seq";      // 状态帧序号（丢包检测）
    constexpr const char *kTs = "ts";        // Unix 时间戳（秒，带小数）
    constexpr const char *kRosAlive = "ros"; // 心跳：桥进程是否活着
    constexpr const char *kMdcAlive = "mdc"; // 心跳：设备是否在报数

    // ------------------------------------------------- 手动充电口（MANUAL_RobotStatus）
    constexpr const char *kManSwitch = "man_sw";           // 充电开关 0关 1开
    constexpr const char *kManBatteryId = "man_bid";       // 电池 ID
    constexpr const char *kManVoltage = "man_v";           // 总电压 mV
    constexpr const char *kManChargeCurrent = "man_ic";    // 充电电流 0.1A
    constexpr const char *kManDischargeCurrent = "man_dc"; // 放电电流 0.1A
    constexpr const char *kManSoc = "man_soc";             // 电量 %
    constexpr const char *kManTemp = "man_tmp";            // 电池温度 ℃

    // --------------------------------------------------- 自动充电口（AUTO_RobotStatus）
    constexpr const char *kAutoSwitch = "auto_sw";           // 充电开关 0关 1开
    constexpr const char *kAutoBatteryId = "auto_bid";       // 电池 ID
    constexpr const char *kAutoVoltage = "auto_v";           // 总电压 mV
    constexpr const char *kAutoChargeCurrent = "auto_ic";    // 充电电流 0.1A
    constexpr const char *kAutoDischargeCurrent = "auto_dc"; // 放电电流 0.1A
    constexpr const char *kAutoSoc = "auto_soc";             // 电量 %
    constexpr const char *kAutoTemp = "auto_tmp";            // 电池温度 ℃

    // ---------------------------------------------------------------- 充电桩状态
    // CCU 电机状态 1初始化中 2初始化完成 3回零中 4回零完成 5到位中 6到位完成
    //              0xFE 断联  0xFF 错误
    constexpr const char *kCcuMotorStatus = "ccu_motor";
    constexpr const char *kChargeMode = "chg_mode";   // 0待机 1手动 2自动
    constexpr const char *kChargeState = "chg_state"; // 0故障 1空闲 2充电中 3充满
    constexpr const char *kFanStatus = "fan";         // 风扇 0关 1开
    constexpr const char *kHallSwitch = "hall";       // 霍尔开关 0未触发 1触发
    constexpr const char *kManualLink = "man_link";   // 手动线连接 0未连接 1已连接
    constexpr const char *kManualCharge = "man_chg";  // 手动充电 0关 1开
    constexpr const char *kAutoLink = "auto_link";    // 自动充电连接 0未连接 1已连接
    constexpr const char *kAutoCharge = "auto_chg";   // 自动充电 0关 1开

    // ---------------------------------------------------------------- 默认端口
    /** 界面监听的 UDP 端口（桥被动往这里发，不需要界面回包） */
    constexpr int kDefaultStatPort = 8891;

    // ---------------------------------------------------------------- 其它阈值
    /**
     * 超过这么久没收到状态帧，判定设备失联。
     * 设备约 500ms 一帧且"仅在有新增帧时发布"，所以实际间隔可能更长，
     * 留到 2 秒（约 4 帧）避免误报。
     */
    constexpr int kMdcStaleMs = 2000;

} // namespace mdcproto

#endif // MDC_PROTOCOL_H
