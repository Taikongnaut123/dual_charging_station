# MDC 接口使用指南（订阅 / 下发）

本指南汇总 `mdc_serial` 节点对外提供的全部 ROS2 接口，分为 **上行订阅**（设备状态上报）与 **下发指令**（Service 控制）两部分。

- 节点名：`mdc_node`
- 包名：`mdc_serial`
- 上行数据话题：`/mdc_data`（10Hz）
- 下发服务前缀：`/mdc/*`

> 命令行示例中的 `mdc_serial/srv/Xxx` 即 `ros2 service call` 使用的类型名。

---

## 一、上行订阅（设备 → 主机）

### 1.1 `/mdc_data` — 设备全量状态上报

| 项目 | 值 |
|------|-----|
| 话题名 | `/mdc_data` |
| 消息类型 | `mdc_serial/msg/MdcData` |
| 频率 | 10 Hz（设备约每 98ms 上报一帧，仅当有新帧时发布） |
| 方向 | 订阅（设备 → 主机） |

字段按功能分组，单位见说明列。

#### 电池状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `battery_total_voltage` | int32 | 总电压 (mV) |
| `battery_charge_current` | int32 | 充电电流 (mA) |
| `battery_discharge_current` | int32 | 放电电流 (mA) |
| `battery_soc` | int32 | **电池实时电量 (%)** |
| `battery_temp` | int32 | 温度 (℃) |

#### 电源管理（PMC）

| 字段 | 类型 | 说明 |
|------|------|------|
| `pmc_total_voltage` | int32 | 总输入电压 (mV) |
| `pmc_total_current` | int32 | 总输入电流 (mA) |
| `pmc_head_voltage` / `pmc_head_current` | int32 | 头部电机电压 (mV) / 电流 (mA) |
| `pmc_right_voltage` / `pmc_right_current` | int32 | 右腿电机电压 / 电流 |
| `pmc_left_voltage` / `pmc_left_current` | int32 | 左腿电机电压 / 电流 |
| `pmc_nonisolated_voltage` / `pmc_nonisolated_current` | int32 | 12V 非隔离 DCDC 电压 / 电流 |
| `pmc_isolated_current` | int32 | 12V 隔离 DCDC 电流 (mA) |
| `pmc_alarm_status` | int32 | 报警状态 |
| `pmc_emergency_status` | int32 | 紧急状态 |

#### 电源 IO 状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `powerio_vo1` ~ `powerio_vo4` | uint8 | 电源输出通道 1-4 状态 |
| `powerio_sw1` / `powerio_sw2` | uint8 | 拨码开关 1 / 2 |
| `powerio_relay1` / `powerio_relay2` | uint8 | 继电器 1 / 2 状态 |
| `powerio_echo1` | uint8 | 光电输入 1 状态 |
| `robot_seated` | uint8 | **机器人是否坐下 (0:未坐下 1:坐下)**，由光电输入 echo2 复用 |

#### 红外状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `ir_bs` | uint8 | 电池充电状态 (1:充电中) |
| `ir_l_a` / `ir_l_b` | uint8 | 左侧 A / B 管信号 |
| `ir_r_a` / `ir_r_b` | uint8 | 右侧 A / B 管信号 |
| `ir_sh` | uint8 | 停车握手状态 (1:成功) |

#### 风扇状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `fan1_pwm` ~ `fan4_pwm` | uint8 | 风扇 1-4 PWM (0-100) |
| `fan_temp` | int32 | 当前温度 |

#### LED 状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `led_ch1_data` / `led_ch2_data` / `led_ch3_data` | uint8[8] | LED 通道 1-3 原始数据 |
| `led_rgb_data` | uint8[16] | RGB 灯原始数据 |
| `led_brightness` | int32 | 亮度值 |

#### 无线充电状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `wch_request_flag` | uint8 | 请求标志 |
| `wch_busy` | uint8 | 忙标志 |
| `wch_result` | uint8 | 执行结果 (0:空闲 1:处理中 2:成功 3:失败) |
| `wch_reserved` | uint8 | 保留 |
| `wch_tx_data` | uint8[4] | 最近一次下发载荷 |
| `wch_rx_data` | uint8[4] | 无线端应答载荷 |
| `wch_update_tick` | uint32 | 结果更新时系统 tick (1tick=1ms) |

#### 查看方式

```bash
ros2 topic echo /mdc_data                 # 全部字段
ros2 topic echo /mdc_data battery_soc     # 只看电池电量
ros2 topic echo /mdc_data robot_seated    # 只看坐下信号
ros2 topic hz /mdc_data                   # 确认 ~10Hz
```


---

## 二、下发指令（主机 → 设备，Service）

共 5 个 Service，响应统一为 `bool success` + `string message`。`success=true` 仅代表指令发送成功，多数需再查询 `/mdc_data` 中对应状态字段确认执行结果。

### 2.1 `/mdc/fan_ctrl` — 风扇控制（0xF1）

| 请求字段 | 类型 | 说明 |
|----------|------|------|
| `fan1_pwm` ~ `fan4_pwm` | uint8 | 风扇 1-4 PWM (0-100) |

```bash
ros2 service call /mdc/fan_ctrl mdc_serial/srv/FanCtrl \
  "{fan1_pwm: 80, fan2_pwm: 80, fan3_pwm: 50, fan4_pwm: 0}"
```

### 2.2 `/mdc/powerio_ctrl` — 电源 IO 控制（0xF2）

| 请求字段 | 类型 | 说明 |
|----------|------|------|
| `ctrl` | uint8 | 位掩码 bit0~bit3 → VO1~VO4，**1:关 0:开** |

```bash
# 全部关闭（所有位=1）
ros2 service call /mdc/powerio_ctrl mdc_serial/srv/PowerioCtrl "{ctrl: 0xf}"
# 全部打开（所有位=0）
ros2 service call /mdc/powerio_ctrl mdc_serial/srv/PowerioCtrl "{ctrl: 0x00}"
# 只关 VO1
ros2 service call /mdc/powerio_ctrl mdc_serial/srv/PowerioCtrl "{ctrl: 0x01}"
```

### 2.3 `/mdc/relay_ctrl` — 继电器控制（0xF3）

| 请求字段 | 类型 | 说明 |
|----------|------|------|
| `relay1` | uint8 | 继电器1 (0:释放 1:吸合) |
| `relay2` | uint8 | 继电器2 (0:释放 1:吸合) |

```bash
ros2 service call /mdc/relay_ctrl mdc_serial/srv/RelayCtrl "{relay1: 1, relay2: 0}"
```

### 2.4 `/mdc/wireless_charge` — 无线充电（0xF4）

| 请求字段 | 类型 | 说明 |
|----------|------|------|
| `payload` | uint8[4] | 4 字节透传载荷（避免使用 `0xFFFFFFFF`） |

```bash
ros2 service call /mdc/wireless_charge mdc_serial/srv/WirelessCharge \
  "{payload: [0x01, 0x01, 0x01, 0x01]}"
```

**结果查询**：下发后约 2s 查询 `/mdc_data` 的 `wch_result`（0:空闲 1:处理中 2:成功 3:失败）。

**使用要点**：
- 一次只下发一条，待 `wch_result` 跳变为 2 或 3 后再发下一条；
- 超时门限内不要重发（重发会使 `wch_result` 回到 1）；
- 未充电时应答约 5-6s（内置 3 次重试），最坏约 18s；
- 转发条件：① 无线充电模式 ② 有线充电无数据 ③ 霍尔 PC5 置位（可通过 `robot_seated`/echo2 观测）；
- 有线充电优先，无线转发期间若收到有线数据会立即切到有线路径且不自动恢复。

### 2.5 `/mdc/led_ctrl` — LED 控制（0xF5）

| 请求字段 | 类型 | 说明 |
|----------|------|------|
| `ch1_mode` / `ch2_mode` / `ch3_mode` | uint8 | 通道模式 (0:常亮 1:快闪 2:慢闪 3:呼吸 4:RGB快闪 5:RGB慢闪) |
| `ch1_duty` / `ch2_duty` / `ch3_duty` | uint8 | 通道亮度 (0-100) |
| `rgb_mode` | uint8 | RGB 模式 (0~5) |
| `r_duty` / `g_duty` / `b_duty` | uint8 | 红 / 绿 / 蓝亮度 (0-100) |

```bash
# 通道1 常亮 80%，RGB 红色 100%
ros2 service call /mdc/led_ctrl mdc_serial/srv/LedCtrl \
  "{ch1_mode: 0, ch1_duty: 80, ch2_mode: 0, ch2_duty: 0, ch3_mode: 0, ch3_duty: 0, rgb_mode: 0, r_duty: 100, g_duty: 0, b_duty: 0}"
```

---

## 三、快速上手

```bash
# 1. 启动 MDC 节点
ros2 launch mdc_serial mdc_node.launch.py

# 2. 查看可用话题与服务
ros2 topic list
ros2 service list

# 3. 查看某服务的类型定义
ros2 interface show mdc_serial/srv/PowerioCtrl

# 4. 监听上报数据
ros2 topic echo /mdc_data
```

---

## 四、注意事项

- 修改 `msg/MdcData.msg` 或 `srv/*.srv` 后需 `colcon build` 重新编译并 `source install/setup.bash` 生效。
- `robot_seated` 为 ROS2 层语义字段，底层协议对应电源 IO 的 `echo2`。
- 电源 IO 的 `ctrl` 位定义以节点实际行为为准：**1=关，0=开**（`srv/PowerioCtrl.srv` 中注释与此相反，请以本指南和 README 为准）。
- Service 的 `success` 仅表示串口下发成功，硬件执行结果需回查 `/mdc_data` 相应状态字段。
