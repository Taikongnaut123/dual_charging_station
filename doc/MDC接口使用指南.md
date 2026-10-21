# 充电桩接口使用指南（订阅）

本指南汇总 `mdc_serial` 节点对外提供的 ROS2 接口。

充电桩 UART4 协议为 **只收不发**：充电桩主机周期上报状态帧（类型 `0x12`），RK3588 侧仅接收解析，**不提供任何下行控制 Service**。

- 节点名：`mdc_node`
- 包名：`mdc_serial`
- 上行数据话题：`/mdc_data`
- 消息类型：`mdc_serial/msg/MdcData`

---

## 一、上行订阅（充电桩 → 充电桩主机）

### 1.1 `/mdc_data` — 充电桩状态上报

| 项目 | 值 |
|------|-----|
| 话题名 | `/mdc_data` |
| 消息类型 | `mdc_serial/msg/MdcData` |
| 频率 | 设备约每 500ms 上报一帧，仅当有新增帧时发布 |
| 方向 | 订阅（充电桩 → 主机） |

字段按功能分组，单位见说明列。数值均为协议原始值。

#### 消息头（std_msgs/Header）

| 字段 | 类型 | 说明 |
|------|------|------|
| `header.stamp` | builtin_interfaces/Time | 发布时刻节点时间戳 (`sec` + `nanosec`) |
| `header.frame_id` | string | 固定为 `charge_station` |

#### 手动充电口电池状态（MANUAL_RobotStatus）

| 字段 | 类型 | 说明 |
|------|------|------|
| `manual_chg_switch` | uint8 | 充电开关 (0:关 1:开) |
| `manual_battery_id` | uint8 | 电池 ID |
| `manual_total_voltage` | uint16 | 总电压 (mV) |
| `manual_charge_current` | uint16 | 充电电流 (0.1A) |
| `manual_discharge_current` | uint16 | 放电电流 (0.1A) |
| `manual_soc` | uint16 | 电量 (%) |
| `manual_temp` | uint16 | 电池温度 (℃) |

#### 自动充电口电池状态（AUTO_RobotStatus）

| 字段 | 类型 | 说明 |
|------|------|------|
| `auto_chg_switch` | uint8 | 充电开关 (0:关 1:开) |
| `auto_battery_id` | uint8 | 电池 ID |
| `auto_total_voltage` | uint16 | 总电压 (mV) |
| `auto_charge_current` | uint16 | 充电电流 (0.1A) |
| `auto_discharge_current` | uint16 | 放电电流 (0.1A) |
| `auto_soc` | uint16 | 电量 (%) |
| `auto_temp` | uint16 | 电池温度 (℃) |

#### 充电桩状态

| 字段 | 类型 | 说明 |
|------|------|------|
| `ccu_motor_status` | uint8 | CCU 电机状态 (1:初始化中 2:初始化完成 3:回零中 4:回零完成 5:到位中 6:到位完成 0xFE:断联 0xFF:错误) |
| `charge_mode` | uint8 | 充电模式 (0:待机 1:手动 2:自动) |
| `charge_state` | uint8 | 充电状态 (0:故障 1:空闲 2:充电中 3:充满) |
| `fan_status` | int32 | 风扇 (0:关 1:开) |
| `hall_switch` | int32 | 霍尔开关 (0:未触发 1:触发) |
| `manual_link_status` | int32 | 手动线连接 (0:未连接 1:已连接) |
| `manual_charge_status` | int32 | 手动充电 (0:关 1:开) |
| `auto_link_status` | int32 | 自动充电连接 (0:未连接 1:已连接) |
| `auto_charge_status` | int32 | 自动充电 (0:关 1:开) |

#### 查看方式

```bash

source /opt/ros/humble/setup.bash
source /root/ros2_ws/install/setup.bash
export ROS_DOMAIN_ID=122
ros2 topic echo /mdc_data                    # 全部字段
ros2 topic hz /mdc_data                      # 确认上报频率 (~2Hz)
```

> 注意：`ros2 topic echo` 的第二个位置参数是**消息类型**（不是字段名），
> 写 `ros2 topic echo /mdc_data charge_state` 会报 `The passed message type is invalid`。
> 该命令不支持只订阅单个字段，如需观察某一字段可用管道过滤：
>
> ```bash
> ros2 topic echo /mdc_data | grep --line-buffered charge_state
> ```
>
> 仅当类型自动检测失败时，才需显式补全类型：
> `ros2 topic echo /mdc_data mdc_serial/msg/MdcData`

---


## 二、注意事项

- 协议只收不发，节点不提供下行 Service；充电控制由充电桩主机自主完成。
- 电流字段单位为 0.1A（如 `charge_current=150` 表示 15.0A），电压为 mV，使用方按需换算。
- 修改 `ros2/msg/MdcData.msg` 后需 `colcon build` 重新编译并 `source install/setup.bash` 生效。
- 帧校验（帧头/类型/长度/帧尾/校验和）在协议层完成，只有校验通过的帧才会发布。
