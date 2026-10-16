# MDC 上行全数据上报 (设备→主机, 0x11帧)
# 约每 98ms 上报一次, Topic 发布频率 10Hz

# ---- 电池状态 (20 字节) ----
int32 battery_total_voltage       # 总电压 (mV)
int32 battery_charge_current      # 充电电流 (mA)
int32 battery_discharge_current   # 放电电流 (mA)
int32 battery_soc                 # 电量 (%)
int32 battery_temp                # 温度 (℃)

# ---- 电源管理 (52 字节) ----
int32 pmc_total_voltage           # 总输入电压 (mV)
int32 pmc_total_current           # 总输入电流 (mA)
int32 pmc_head_voltage            # 头部电机电压 (mV)
int32 pmc_head_current            # 头部电机电流 (mA)
int32 pmc_right_voltage           # 右腿电机电压 (mV)
int32 pmc_right_current           # 右腿电机电流 (mA)
int32 pmc_left_voltage            # 左腿电机电压 (mV)
int32 pmc_left_current            # 左腿电机电流 (mA)
int32 pmc_nonisolated_voltage     # 12V非隔离DCDC电压 (mV)
int32 pmc_nonisolated_current     # 12V非隔离DCDC电流 (mA)
int32 pmc_isolated_current        # 12V隔离DCDC电流 (mA)
int32 pmc_alarm_status            # 报警状态
int32 pmc_emergency_status        # 紧急状态

# ---- 电源IO状态 (10 字节) ----
uint8 powerio_vo1                 # 电源输出通道1
uint8 powerio_vo2                 # 电源输出通道2
uint8 powerio_vo3                 # 电源输出通道3
uint8 powerio_vo4                 # 电源输出通道4
uint8 powerio_sw1                 # 拨码开关1
uint8 powerio_sw2                 # 拨码开关2
uint8 powerio_relay1              # 继电器1
uint8 powerio_relay2              # 继电器2
uint8 powerio_echo1               # 光电输入1
uint8 robot_seated                # 机器人是否坐下 (0:未坐下 1:坐下)

# ---- 红外状态 (6 字节) ----
uint8 ir_bs                       # 电池充电状态 (1:充电中)
uint8 ir_l_a                      # 左侧A管信号
uint8 ir_l_b                      # 左侧B管信号
uint8 ir_r_a                      # 右侧A管信号
uint8 ir_r_b                      # 右侧B管信号
uint8 ir_sh                       # 停车握手状态 (1:成功)

# ---- 风扇状态 (8 字节) ----
uint8 fan1_pwm                    # 风扇1 PWM (0-100)
uint8 fan2_pwm                    # 风扇2 PWM (0-100)
uint8 fan3_pwm                    # 风扇3 PWM (0-100)
uint8 fan4_pwm                    # 风扇4 PWM (0-100)
int32 fan_temp                    # 当前温度

# ---- LED状态 (44 字节) ----
uint8[8] led_ch1_data             # LED通道1原始数据
uint8[8] led_ch2_data             # LED通道2原始数据
uint8[8] led_ch3_data             # LED通道3原始数据
uint8[16] led_rgb_data            # RGB灯原始数据
int32 led_brightness              # 亮度值

# ---- 无线充电状态 (16 字节) ----
uint8 wch_request_flag            # 请求标志
uint8 wch_busy                    # 忙标志
uint8 wch_result                  # 执行结果 (0:空闲 1:处理中 2:成功 3:失败)
uint8 wch_reserved                # 保留
uint8[4] wch_tx_data              # 最近一次下发载荷
uint8[4] wch_rx_data              # 无线端应答载荷
uint32 wch_update_tick            # 结果更新时系统tick (1tick=1ms)
