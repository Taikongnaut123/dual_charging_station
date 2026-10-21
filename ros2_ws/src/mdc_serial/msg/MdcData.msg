# 充电桩状态帧 (ChargeStation, 类型 0x12) - 设备→主机
# 充电桩主机约每 500ms 上报一帧，节点仅在有新增帧时发布

std_msgs/Header header

# ---- 手动充电口电池状态 (MANUAL_RobotStatus) ----
uint8  manual_chg_switch          # 充电开关 0:关 1:开
uint8  manual_battery_id          # 电池 ID
uint16 manual_total_voltage       # 总电压 (mV)
uint16 manual_charge_current      # 充电电流 (0.1A)
uint16 manual_discharge_current   # 放电电流 (0.1A)
uint16 manual_soc                 # 电量 (%)
uint16 manual_temp                # 电池温度 (℃)

# ---- 自动充电口电池状态 (AUTO_RobotStatus) ----
uint8  auto_chg_switch            # 充电开关 0:关 1:开
uint8  auto_battery_id            # 电池 ID
uint16 auto_total_voltage         # 总电压 (mV)
uint16 auto_charge_current        # 充电电流 (0.1A)
uint16 auto_discharge_current     # 放电电流 (0.1A)
uint16 auto_soc                   # 电量 (%)
uint16 auto_temp                  # 电池温度 (℃)

# ---- 充电桩状态 ----
uint8 ccu_motor_status            # CCU 电机状态 (1:初始化中 2:初始化完成 3:回零中 4:回零完成 5:到位中 6:到位完成 0xFE:断联 0xFF:错误)
uint8 charge_mode                 # 充电模式 (0:待机 1:手动 2:自动)
uint8 charge_state                # 充电状态 (0:故障 1:空闲 2:充电中 3:充满)
int32 fan_status                  # 风扇 (0:关 1:开)
int32 hall_switch                 # 霍尔开关 (0:未触发 1:触发)
int32 manual_link_status          # 手动线连接 (0:未连接 1:已连接)
int32 manual_charge_status        # 手动充电 (0:关 1:开)
int32 auto_link_status            # 自动充电连接 (0:未连接 1:已连接)
int32 auto_charge_status          # 自动充电 (0:关 1:开)
