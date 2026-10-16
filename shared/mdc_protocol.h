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
 *  传输：UDP，一包一条 JSON（单行、无换行）。纯 JSON 数字/字符串/数组，
 *        不用嵌套对象 —— 目的是让桥节点能用几十行手写序列化，不引入
 *        nlohmann/json 之类的第三方依赖。
 *
 *  ── 上行（桥 → 界面）──
 *    状态帧  10Hz：{"t":"s","seq":123,"ts":1729.5, ...51 个字段...}
 *    心跳    1Hz ：{"t":"hb","ros":1,"mdc":0,"ts":1729.5}
 *                  ros=1 桥进程活着；mdc=1 最近 1s 内收到过设备数据
 *    控制应答     ：{"t":"ack","id":17,"cmd":"relay","ok":true,"msg":"..."}
 *
 *  ── 下行（界面 → 桥）──
 *    控制请求     ：{"t":"cmd","id":17,"cmd":"relay","relay1":1,"relay2":0}
 *
 *  界面侧的三态判定（很重要，能区分"桥没起来"和"设备没数据"）：
 *    · 连状态帧和心跳都收不到  -> 桥进程未运行 / 网络不通
 *    · 只有心跳、mdc=0         -> 桥正常，但设备没上报（串口未接 / 节点未起来）
 *    · 收到状态帧              -> 数据正常
 *
 *  单位约定：与 MdcData.msg 完全一致，**网络层不做任何换算**，
 *            mV/mA 原样传输，换算交给界面显示时处理。
 */

namespace mdcproto
{

    // ---------------------------------------------------------------- 包类型
    constexpr const char *kType = "t";
    constexpr const char *kTypeState = "s";      // 桥 -> 界面：设备状态帧
    constexpr const char *kTypeHeartbeat = "hb"; // 桥 -> 界面：心跳
    constexpr const char *kTypeCommand = "c";    // 界面 -> 桥：控制请求
    constexpr const char *kTypeAck = "ack";      // 桥 -> 界面：控制应答
    // 说明：包类型值故意不用 "cmd"，因为键 kCmd 也叫 "cmd"，
    //       {"t":"cmd","cmd":"relay"} 这种报文容易引起混淆与解析歧义。

    // ---------------------------------------------------------------- 通用键
    constexpr const char *kSeq = "seq";      // 状态帧序号（丢包检测）
    constexpr const char *kTs = "ts";        // Unix 时间戳（秒，带小数）
    constexpr const char *kId = "id";        // 控制请求序号（应答按此匹配）
    constexpr const char *kCmd = "cmd";      // 控制命令名
    constexpr const char *kOk = "ok";        // 应答：是否成功
    constexpr const char *kMsg = "msg";      // 应答：提示信息
    constexpr const char *kRosAlive = "ros"; // 心跳：桥进程是否活着
    constexpr const char *kMdcAlive = "mdc"; // 心跳：设备是否在报数

    // ---------------------------------------------------------------- 电池状态
    constexpr const char *kBatVoltage = "bat_v";           // 总电压 mV
    constexpr const char *kBatChargeCurrent = "bat_ic";    // 充电电流 mA
    constexpr const char *kBatDischargeCurrent = "bat_id"; // 放电电流 mA
    constexpr const char *kBatSoc = "bat_soc";             // 电量 %
    constexpr const char *kBatTemp = "bat_tmp";            // 温度 ℃

    // ---------------------------------------------------------------- 电源管理 PMC
    constexpr const char *kPmcTotalVoltage = "pmc_tv";       // 总输入电压 mV
    constexpr const char *kPmcTotalCurrent = "pmc_tc";       // 总输入电流 mA
    constexpr const char *kPmcHeadVoltage = "pmc_hv";        // 头部电机电压 mV
    constexpr const char *kPmcHeadCurrent = "pmc_hc";        // 头部电机电流 mA
    constexpr const char *kPmcRightVoltage = "pmc_rv";       // 右腿电机电压 mV
    constexpr const char *kPmcRightCurrent = "pmc_rc";       // 右腿电机电流 mA
    constexpr const char *kPmcLeftVoltage = "pmc_lv";        // 左腿电机电压 mV
    constexpr const char *kPmcLeftCurrent = "pmc_lc";        // 左腿电机电流 mA
    constexpr const char *kPmcNonisolatedVoltage = "pmc_nv"; // 12V 非隔离 DCDC 电压 mV
    constexpr const char *kPmcNonisolatedCurrent = "pmc_nc"; // 12V 非隔离 DCDC 电流 mA
    constexpr const char *kPmcIsolatedCurrent = "pmc_ic";    // 12V 隔离 DCDC 电流 mA
    constexpr const char *kPmcAlarmStatus = "pmc_alarm";     // 报警状态
    constexpr const char *kPmcEmergencyStatus = "pmc_emg";   // 紧急状态

    // ---------------------------------------------------------------- 电源 IO
    constexpr const char *kIoVo1 = "io_vo1";       // 电源输出通道 1
    constexpr const char *kIoVo2 = "io_vo2";       // 电源输出通道 2
    constexpr const char *kIoVo3 = "io_vo3";       // 电源输出通道 3
    constexpr const char *kIoVo4 = "io_vo4";       // 电源输出通道 4
    constexpr const char *kIoSw1 = "io_sw1";       // 拨码开关 1
    constexpr const char *kIoSw2 = "io_sw2";       // 拨码开关 2
    constexpr const char *kIoRelay1 = "io_rly1";   // 继电器 1
    constexpr const char *kIoRelay2 = "io_rly2";   // 继电器 2
    constexpr const char *kIoEcho1 = "io_echo1";   // 光电输入 1
    constexpr const char *kIoSeated = "io_seated"; // 机器人是否坐下

    // ---------------------------------------------------------------- 红外状态
    constexpr const char *kIrBs = "ir_bs"; // 电池充电状态 (1:充电中)
    constexpr const char *kIrLA = "ir_la"; // 左侧 A 管
    constexpr const char *kIrLB = "ir_lb"; // 左侧 B 管
    constexpr const char *kIrRA = "ir_ra"; // 右侧 A 管
    constexpr const char *kIrRB = "ir_rb"; // 右侧 B 管
    constexpr const char *kIrSh = "ir_sh"; // 停车握手状态 (1:成功)

    // ---------------------------------------------------------------- 风扇状态
    constexpr const char *kFan1 = "fan1";       // 风扇 1 PWM
    constexpr const char *kFan2 = "fan2";       // 风扇 2 PWM
    constexpr const char *kFan3 = "fan3";       // 风扇 3 PWM
    constexpr const char *kFan4 = "fan4";       // 风扇 4 PWM
    constexpr const char *kFanTemp = "fan_tmp"; // 当前温度 ℃

    // ---------------------------------------------------------------- LED 状态
    constexpr const char *kLedCh1 = "led_ch1"; // uint8[8]
    constexpr const char *kLedCh2 = "led_ch2"; // uint8[8]
    constexpr const char *kLedCh3 = "led_ch3"; // uint8[8]
    constexpr const char *kLedRgb = "led_rgb"; // uint8[16]
    constexpr const char *kLedBrightness = "led_br";

    // ---------------------------------------------------------------- 无线充电状态
    constexpr const char *kWchRequestFlag = "wch_req";
    constexpr const char *kWchBusy = "wch_busy";
    constexpr const char *kWchResult = "wch_res"; // 0空闲 1处理中 2成功 3失败
    constexpr const char *kWchReserved = "wch_rsv";
    constexpr const char *kWchTxData = "wch_tx";       // uint8[4]
    constexpr const char *kWchRxData = "wch_rx";       // uint8[4]
    constexpr const char *kWchUpdateTick = "wch_tick"; // ms

    // ---------------------------------------------------------------- 控制命令名
    constexpr const char *kCmdFan = "fan";         // -> /mdc/fan_ctrl
    constexpr const char *kCmdPowerio = "powerio"; // -> /mdc/powerio_ctrl
    constexpr const char *kCmdRelay = "relay";     // -> /mdc/relay_ctrl
    constexpr const char *kCmdWch = "wch";         // -> /mdc/wireless_charge
    constexpr const char *kCmdLed = "led";         // -> /mdc/led_ctrl

    // ---------------------------------------------------------------- 默认端口
    constexpr int kDefaultCmdPort = 8890;  // 桥监听（界面 -> 桥）
    constexpr int kDefaultStatPort = 8891; // 界面监听（桥 -> 界面）

    // ---------------------------------------------------------------- 其它阈值
    constexpr int kMdcStaleMs = 1000;   // 超过这么久没收到状态帧，判定设备失联
    constexpr int kAckTimeoutMs = 3000; // 界面等待控制应答的超时

} // namespace mdcproto

#endif // MDC_PROTOCOL_H
