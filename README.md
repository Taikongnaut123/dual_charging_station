# 双轮足机器人充电桩 · 显示屏程序

充电桩一体屏上的监控界面：接收下位机上报的充电桩状态，实时显示两个充电口的数据、
CCU 电机进度和整桩状态。

> **协议版本：0x12 充电桩状态帧（只收不发）**
> 充电控制由充电桩主机自主完成，本程序是**纯监控**界面，不提供任何下行控制。

---

## 1. 系统架构

```
充电桩主机 (MCU)
   │  UART4，0x12 状态帧，约 500ms 一帧，只收不发
   ▼
mdc_node  （ROS2 节点，在机器人/主机侧，不在本工程）
   │  topic /mdc_data，消息类型 mdc_serial/msg/MdcData
   │  传输 = DDS，序列化 = rosidl 生成
   ▼
mdc_bridge  ◄──「翻译官」：一边一套语言
   │  收：mdc_serial::msg::MdcData（ROS2 生成的结构体）
   │  转：逐字段 → JSON，外加封套 {"t":"s","seq":123,"ts":...}
   ▼
   │  UDP 单行 JSON，约 400 字节，一包一条
   ▼
dual_charging_station  （本程序，纯 Qt）
   │  MdcData::fromJson() 逐字段 → 界面自己的 MdcData 结构体
   ▼
三个页面：充电总览 / 充电桩状态 / 运行日志
```

**为什么最后一段不用 ROS2 直连？**
一体屏装不了也不想装 ROS2 + DDS 那一整套。面板只需要"每 500ms 收一包、显示出来"，
十几行 UDP 代码就够。代价是要自己维护封套、算丢帧、做链路三态判定——换来的是
界面程序**零 ROS2 依赖**，一个 300 多 KB 的二进制就能跑。

**两个同名但不同的 `MdcData`**：桥里的是 rosidl 从 `.msg` 生成的类型，界面里的是
`mdcdata.h` 手写的普通结构体。它们分属两个进程、两套构建系统，中间的 UDP JSON
是唯一的跨界媒介。

---

## 2. 快速开始

```bash
# ① 构建界面程序（qmake）
mkdir -p build && cd build
qmake ../dual_charging_station.pro && make -j$(nproc) && cd ..

# ② 构建 ROS2 桥（colcon）
source /opt/ros/humble/setup.bash
cd ros2_ws && colcon build && cd ..

# ③ 一键演示：假设备 + 桥 + 界面
tools/run_demo.sh
tools/run_demo.sh --loopback   # 只起界面，用内置仿真数据，连 ROS2 都不用装
```

没有真机时 `mdc_fake_pub` 会按真实节奏（约 2Hz）模拟"就位 → 手动口充电 → 空闲 →
自动口充电 → 空闲"的完整循环，用来验证整条链路。

---

## 3. 界面

三个页签（`Ctrl+1/2/3` 切换）：

| 页签 | 内容 |
| --- | --- |
| **充电总览** | 手动口 / 自动口两张卡（SOC 环 + 电压/电流/温度/电池 ID/开关）、整桩概况（模式/状态/本次充电量/已充时长/预计剩余/累计）、双口充电电流曲线 |
| **充电桩状态** | CCU 电机 6 步进度、连接与充电指示灯（6 个）、原始字段一览（对照 `ros2 topic echo` 排障） |
| **运行日志** | 链路状态变化、错误与告警，最新在最上 |

### 链路三态（排障关键）

页眉的链路胶囊有三种状态，能直接区分"桥没起来"和"设备没数据"：

| 状态 | 含义 | 该查什么 |
| --- | --- | --- |
| **未连接** | 连心跳都收不到 | 桥进程没运行 / 网络不通 / 端口不对 |
| **桥已连接** | 桥活着，但设备没上报 | 串口没接 / `mdc_node` 没起来 / QoS 不匹配 |
| **在线** | 正常收到状态帧 | —— |

断连时的界面表现（避免把"冻结的旧数据"误读成"还在正常充电"）：

- **运行日志**页记一条记录，并给出对应排查方向
- **充电总览**的大字变为「数据中断」，副标题说明是"桥没了"还是"设备没上报"
- 环形表的流动动画**停止**（数据不更新还继续转是最误导的一种"假在线"）
- 页眉链路胶囊同时给出颜色分级（红=桥未连接，黄=桥在线但设备无数据）

---

## 4. 目录结构

```
├── main.cpp / mainwindow.*     入口与主窗口（3 页签）
├── bridgeclient.*              UDP 接收（单向，无发送代码）
├── mdcdata.h                   界面侧的数据结构（与 MdcData.msg 一一对应）
├── stationcontroller.*         状态派生 + 本地统计
├── theme.*                     配色与样式表
├── shared/
│   └── mdc_protocol.h          ★ 界面与桥之间的**唯一协议契约**
├── pages/
│   ├── pageutils.h             页面小工具
│   ├── overviewpage.*          充电总览
│   └── stationpage.*           充电桩状态
├── widgets/                    自绘控件（卡片/环形表/曲线/时间轴/指示灯/指标块）
├── ros2_ws/src/
│   ├── mdc_serial/             ROS2 接口包（只有 msg，本地复刻）
│   └── mdc_bridge/             桥 + 假设备
├── tools/
│   ├── run_demo.sh             开发用一键演示（依赖 build/ 与 install/）
│   ├── ci_smoke_test.sh        冒烟测试（本地、CI 共用）
│   ├── make_package.sh         打包成可部署的 tar.gz
│   └── pack/                   打进包里的启动脚本
└── doc/                        下位机接口文档（协议来源）
```

---

## 5. 数据映射（MdcData.msg → JSON → 界面）

### 手动 / 自动充电口（各 7 个字段）

| msg 字段 | JSON 键 | 类型 | 单位 | 界面显示 |
| --- | --- | --- | --- | --- |
| `manual_chg_switch` | `man_sw` | uint8 | 0/1 | 充电开关：开 / 关 |
| `manual_battery_id` | `man_bid` | uint8 | — | 电池 ID |
| `manual_total_voltage` | `man_v` | uint16 | mV | 除以 1000 → V |
| `manual_charge_current` | `man_ic` | uint16 | **0.1A** | 除以 10 → A |
| `manual_discharge_current` | `man_dc` | uint16 | **0.1A** | 除以 10 → A |
| `manual_soc` | `man_soc` | uint16 | % | SOC 环 |
| `manual_temp` | `man_tmp` | uint16 | ℃ | 电池温度 |

自动口同构，键名换成 `auto_sw` / `auto_bid` / `auto_v` / `auto_ic` / `auto_dc` /
`auto_soc` / `auto_tmp`。

### 充电桩状态（9 个字段）

| msg 字段 | JSON 键 | 取值 |
| --- | --- | --- |
| `ccu_motor_status` | `ccu_motor` | 1初始化中 2初始化完成 3回零中 4回零完成 5到位中 6到位完成 **0xFE断联 0xFF错误** |
| `charge_mode` | `chg_mode` | 0待机 1手动 2自动 |
| `charge_state` | `chg_state` | 0故障 1空闲 2充电中 3充满 |
| `fan_status` | `fan` | 0关 1开 |
| `hall_switch` | `hall` | 0未触发 1触发 |
| `manual_link_status` | `man_link` | 0未连接 1已连接 |
| `manual_charge_status` | `man_chg` | 0关 1开 |
| `auto_link_status` | `auto_link` | 0未连接 1已连接 |
| `auto_charge_status` | `auto_chg` | 0关 1开 |

### 充电口的「启用 / 未启用」

`manual_link_status` / `auto_link_status` 决定**这个口当前算不算在用**，
两个口彼此独立，三种组合都支持：

| manual | auto | 界面表现 |
| --- | --- | --- |
| 1 | 1 | 两个口都正常显示 |
| 1 | 0 | 手动口正常，自动口置灰 |
| 0 | 1 | 自动口正常，手动口置灰 |
| 0 | 0 | 两个口都置灰 |

`link_status = 0` 时该卡片的处理：

- 整卡降级：底色压暗 + **虚线边框**，标题/字段名/数值全部转灰
- 徽标从 `MANUAL` / `AUTO` 变成 **`未启用`**
- 六个字段一律显示 `--`：设备可能仍在报上一次的残留值，直接显示会让人
  误以为"这个口接着一块 0A 的电池"
- 环形表归零并转灰（**自绘控件不吃 QSS**，靠 `CircularGauge::setDimmed()`）
- 卡片底部给一行提示，写明是哪一路没接 + 原始字段名，例如
  `手动充电线未连接（manual_link_status = 0），本口未启用`

假设备可以用参数强制指定，方便验证这几种组合：

```bash
# -1 跟随充电状态（默认）  0 都不启用  1 仅手动  2 仅自动  3 都启用
ros2 run mdc_bridge mdc_fake_pub --ros-args -p link_override:=1
```

### 协议里没有、由本机统计的量

界面上这几项都标注了"本地统计/估算"，**不是设备上报的**：

| 界面项 | 怎么算的 |
| --- | --- |
| 本次充电量 | `(当前SOC - 起始SOC)/100 × 额定容量`，默认 1.92 kWh（48V/40Ah，按实际电池组改 `packCapacityKwh`） |
| 本次已充时长 | 进入"充电中"状态后的累计时长 |
| 预计剩余 | `(100-SOC)/100 × 容量 ÷ 当前功率` |
| 累计次数 / 累计电量 | 每次充电结束（离开"充电中"）累加一次，存 `QSettings` |

### Header 为什么不传

`MdcData.msg` 里的 `std_msgs/Header` 是 ROS2 元数据（`stamp` + `frame_id`）。
界面只需要"数据什么时候到的"，用封套里的 `ts` 或本地到达时间就够了；`frame_id`
是固定值 `charge_station`，传过去没有意义。所以 JSON 里省略了整个 header。

---

## 6. 为什么没有控制功能

旧版（0x11 帧）有一套完整的下行链路：界面 → UDP `cmd` → 桥 → ROS2 Service
（`fan_ctrl` / `relay_ctrl` / `powerio_ctrl` / `wireless_charge` / `led_ctrl`）。

**新协议取消了下行**。`doc/MDC接口使用指南.md` 明确写着：

> 充电桩 UART4 协议为**只收不发**，充电桩主机周期上报状态帧（类型 `0x12`），
> RK3588 侧仅接收解析，**不提供任何下行控制 Service**。充电控制由充电桩主机自主完成。

所以 5 个 `.srv`、控制页、以及协议里的 `cmd` / `ack` / `id` 全部删除。
如果将来协议恢复下行控制，需要重新加：`mdc_serial/srv/`、桥的服务客户端、
界面的控制页、以及协议里的一套请求-应答字段。

---

## 7. 快捷键

| 按键 | 作用 |
| --- | --- |
| `F11` / `Esc` | 全屏 / 还原 |
| `Ctrl+1` ~ `Ctrl+3` | 切换页签 |
| `Ctrl+L` | 切换环回仿真模式 |

---

## 8. 接真机

### 8.1 前提

```bash
# 节点在发数据吗？（约 2Hz）
ros2 topic hz /mdc_data

# 类型和我们的复刻包一致吗？
ros2 interface show mdc_serial/msg/MdcData
```

⚠️ **`ROS_DOMAIN_ID` 必须一致**。接口文档里用的是 **122**：

```bash
export ROS_DOMAIN_ID=122       # 桥和 mdc_node 都要
```

`ROS_DOMAIN_ID` 不同时，同一台机器上也是两个隔离的世界——现象是桥收不到数据。

### 8.2 启动

```bash
# 屏上（或同机）
./run_bridge.sh                # 桥：订阅 /mdc_data 并转发 UDP
./run_ui.sh                    # 界面：监听 UDP 8891
```

桥默认往 `127.0.0.1:8891` 发（因为界面从不回包，桥不需要等界面先说话）。
跨机时改成界面的地址：

```bash
./run_bridge.sh --ros-args -p peer_host:=192.168.1.50 -p peer_port:=8891
```

### 8.3 QoS

桥和假设备都用 `SensorDataQoS()`（BEST_EFFORT）。在 DDS 里 **BEST_EFFORT 订阅者
与 RELIABLE / BEST_EFFORT 两种发布者都兼容**；而 RELIABLE 订阅者遇到 BEST_EFFORT
发布者会连不上，且没有明显报错（现象是"一帧都没有"）。设备遥测丢一两帧无所谓，
兼容性更要紧，所以选了 BEST_EFFORT。

### 8.4 排查顺序

按数据流逐跳验证，别跳步：

```
① ros2 topic hz /mdc_data          设备在报数吗
② ros2 topic info /mdc_data --verbose   两边的 QoS / 类型兼容吗
③ 桥日志 "桥已启动"                 桥起来了吗
④ ros2 service list | grep mdc      （新协议下应该没有服务，正常）
⑤ 界面 [统计] 帧在涨 --verbose       UDP 这一跳通了吗
```

### 8.5 本地复刻的接口包

`ros2_ws/src/mdc_serial` 是依据 `doc/` **复刻**的。机器人上如果装了官方包，
**以官方为准**：用官方包重新 `colcon build` 桥即可。两边 msg 必须完全一致，
否则订阅静默失败。

---

## 9. 换肤与适配屏幕

- **配色集中在一个地方**：`theme.h` 里的颜色常量；样式表在 `theme.cpp`，
  用 `@TOKEN@` 占位符替换。改配色只动这两个文件。
- **自绘控件不受样式表影响**：`CircularGauge` / `ChargeCurveChart` /
  `StageTimeline` / `LightIndicator` / `LevelBar` 的取色来自 `theme.h` 常量，
  要走 `setAccent()` 之类的 C++ 接口。
- **小屏适配（8 寸一体屏）**：界面按 **1360×800 设计稿**编写，启动时读主屏
  分辨率自动算两个系数，不靠写死窗口尺寸：

  | 系数 | 计算方式 | 管什么 |
  |---|---|---|
  | `geoScale` | `min(宽/1360, 高/800)`，限制 0.6～1.0 | 间距、控件尺寸、圆角、边框 |
  | `fontScale` | `√geoScale`，限制 0.8～1.0 | 字号，**故意比几何缩得少** |

  字号不跟着几何等比缩，是因为 8 寸屏像素密度远高于普通显示器：1024×768 塞进
  8 寸约 160 DPI，字跟着缩到 0.75 会明显偏小。缩得少一点，行高相对变大，多出来的
  高度由页面外层的 `QScrollArea` 消化——比"字看不清"好。

  | 屏幕 | geoScale | fontScale | 效果 |
  |---|---|---|---|
  | 1920×1080 | 1.00 | 1.00 | 设计稿原尺寸，窗口 1360×800 |
  | 1024×768（8 寸） | 0.75 | 0.87 | 整幅内容放得下，三个页签都不裁切 |
  | 800×480 | 0.60 | 0.80 | 放得下，字号偏小，下方曲线卡片需滚动 |

  新增界面代码时：设计稿里的像素值一律写成 `Theme::px(150)`，样式表里的 `px`
  由 `Theme::styleSheet()` 自动换算，**不要再写死数字**，否则小屏下那部分不会缩。

```bash
# 8 寸 1024x768 一体屏全屏运行（自适应，无需额外参数）
./dual_charging_station -f

# 现场觉得字偏大/偏小，直接调系数，不用改代码重编
./dual_charging_station -f --ui-scale 0.85
```

启动日志的第一条会打印实际生效的屏幕尺寸与两个系数，现场排障先看这条。

### 工控板上的 libGL 报错（可忽略）

在 RK3588 这类板子上启动时，终端可能刷：

```
libGL error: glx: failed to create dri3 screen
libGL error: failed to load driver: rockchip
```

这是 Qt 的 xcb 平台插件在探测 GLX 集成时，板卡厂商的 GPU 驱动不全导致的，
**与本程序无关，也不影响显示**：界面全部由 `QPainter` 光栅绘制，一行 OpenGL 都不用。

程序启动时已自动把 `QT_XCB_GL_INTEGRATION` 置为 `none`（在 `QApplication` 构造
之前，见 `main.cpp`）。该默认值只在外部未指定时生效，需要时可改回
`QT_XCB_GL_INTEGRATION=xcb_glx`。

---

## 10. 持续集成（GitHub Actions）

两个工作流，各编一个架构，都是 Linux：

| 工作流文件 | 运行器 | 产物 |
| --- | --- | --- |
| `.github/workflows/build-x86_64.yml` | `ubuntu-22.04` | `dual_charging_station-linux-x86_64.tar.gz` |
| `.github/workflows/build-arm64.yml` | `ubuntu-22.04-arm` | `dual_charging_station-linux-arm64.tar.gz` |

**触发**：push 到 `master`，或在 Actions 页面手动触发。

**产物管理：不做版本管理。** 两份产物都挂在固定 tag `latest` 的同一个 Release 上，
同名文件直接覆盖（`gh release upload --clobber`），Releases 页面永远只有一份最新的，
下载地址固定不变。

**为什么两个架构都用 22.04**：ROS2 Humble 只支持 22.04；且 24.04（glibc 2.39）
编出来的二进制在 22.04 目标机上会报 `GLIBC_2.38 not found`。arm64 用的是 GitHub 的
arm64 原生运行器（真 aarch64 机器，不是 QEMU 模拟）。

### 冒烟测试 `tools/ci_smoke_test.sh`

CI 里不只验证"编过了"，而是真的跑一遍。本地也能跑：

```bash
tools/ci_smoke_test.sh
```

| 阶段 | 做什么 | 证明了什么 |
| --- | --- | --- |
| 1 | 无头启动界面（环回模式 8 秒） | 二进制能启动、3 个页面都能构造、事件循环在跑 |
| 2 | 假设备 → 桥 → UDP → 界面 | 界面确实收到了约 2Hz 数据流，SOC 解析正确 |
| 3 | 另起一个桥发到 8892，python 逐项核对 | 26 个协议字段一个不少、一个不多 |

阶段 3 能抓住"加了字段但桥上忘了序列化"这类静默 bug——字段名对了但值不传，界面
只会显示 0，不报任何错。

### 产物自包含

`mdc_bridge` 运行时 `dlopen` 若干 `mdc_serial` 接口库（fastrtps / introspection /
generator_c），`ldd` 看不到它们。打包时整套带上（`lib/`），并给两个可执行文件链了
`RPATH=$ORIGIN/../lib`。

⚠️ 必须带 `-Wl,--disable-new-dtags`：默认链出的是 `DT_RUNPATH`，而 **`DT_RUNPATH`
对 `dlopen` 无效**，那几个运行时加载的库照样找不到。详见
`ros2_ws/src/mdc_bridge/CMakeLists.txt` 的注释，**不要删那行**。

---

## 11. 部署到目标机

从 Releases 下载对应架构的包（`latest` 标签），解包后：

```
bin/dual_charging_station   显示屏界面（不依赖 ROS2）
bin/mdc_bridge              ROS2 桥
bin/mdc_fake_pub            假设备
lib/libmdc_serial__*.so     mdc_serial 接口库，桥运行时 dlopen 它们
run_ui.sh                   起界面
run_bridge.sh               起桥
run_fake.sh                 起假设备
run_demo.sh                 一键起全套
```

### 目标机依赖分两档

| 只跑界面 | 还要跑桥 / 假设备 |
| --- | --- |
| `sudo apt install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5` | 上面那些 **+ ROS2**（必须同发行版，如 `ros-humble-ros-base`） |

**桥是 ROS2 节点，目标机没装 ROS2 就直接跑不起来**；界面程序不受影响。

### 日常怎么用

```bash
./run_ui.sh --loopback        # 只有界面，内置仿真数据，连 ROS2 都不用装
./run_ui.sh                   # 界面监听本机 UDP 8891
./run_demo.sh                 # 无真机：假设备 + 桥 + 界面
./run_demo.sh --no-fake       # 真机 mdc_node 已在跑，只起桥 + 界面
```

一体屏开机自启通常把 `./run_ui.sh -f` 写进 systemd 或 `~/.xinitrc`。

### 端口

只有一个端口需要关心：**界面的 `--stat-port`（默认 8891）**。

```bash
./dual_charging_station --stat-port 9001     # 换端口
./run_bridge.sh --ros-args -p peer_port:=9001  # 桥必须发到同一端口
```

改了一边不改另一边，现象是**界面永远 0 帧**（数据发到 8891 去了，没人接）。
