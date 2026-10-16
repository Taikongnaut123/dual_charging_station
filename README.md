# 双轮足机器人充电桩 · 显示屏程序

基于 **Qt 5 Widgets** 的充电桩一体屏人机界面。界面全部使用 `QPainter` 自绘 + Qt 样式表，
不依赖 QtCharts / QML，方便部署到资源受限的工控屏，也便于按客户要求定制视觉。

数据来自下位机的 MDC 控制器（ROS2 Humble），但**本程序不依赖 ROS2**——
中间隔了一个独立的桥进程，界面可以部署到没有 ROS2 环境的一体屏上。

---

## 1. 系统架构

```
┌──────────────────┐  串口   ┌───────────────────────┐
│  MDC 控制器       │ ──────► │  mdc_node (ROS2 节点) │
│  (下位机)         │         │  包 mdc_serial        │
└──────────────────┘         └───────────┬───────────┘
                                         │ /mdc_data  10Hz
                                         │ /mdc/* 5 个 Service
                                         ▼
                             ┌───────────────────────┐
                             │  mdc_bridge           │  ← 本仓库 ros2_ws/
                             │  订阅→UDP JSON         │
                             │  UDP→调用 Service      │
                             └───────────┬───────────┘
                                         │ UDP JSON
                                         │ shared/mdc_protocol.h
                                         ▼
                             ┌───────────────────────┐
                             │  dual_charging_station │  ← 本程序（纯 Qt，零 ROS2 依赖）
                             └───────────────────────┘
```

- **上行**：`/mdc_data`（10 Hz）→ 桥序列化成 JSON → UDP → 界面
- **下行**：界面发 UDP JSON 命令 → 桥调用对应 Service → 应答回传界面
- 两个构建系统**共用同一份协议头文件** `shared/mdc_protocol.h`，
  键名只定义一次，避免两端手抄字符串对不上

---

## 2. 快速开始

```bash
# ① 构建界面程序
mkdir -p build && cd build && qmake ../dual_charging_station.pro && make -j$(nproc) && cd ..

# ② 构建 ROS2 桥
source /opt/ros/humble/setup.bash
cd ros2_ws && colcon build && cd ..

# ③ 一键演示（假设备 + 桥 + 界面）
tools/run_demo.sh
```

三种运行方式：

| 命令 | 场景 |
| --- | --- |
| `tools/run_demo.sh` | 假设备 + 桥 + 界面，**无需任何硬件**，可完整体验含控制下发 |
| `tools/run_demo.sh --no-fake` | 只起桥 + 界面，接真机（假定 `mdc_node` 已在运行） |
| `tools/run_demo.sh --loopback` | 只起界面，用内置仿真数据，**连 ROS2 都不用装** |

界面程序也可以单独运行：

```bash
./build/dual_charging_station                     # 连接本机 mdc_bridge（默认 UDP 8890/8891）
./build/dual_charging_station --loopback          # 内置仿真，不连桥
./build/dual_charging_station -f                  # 全屏（一体屏实际运行方式）
./build/dual_charging_station --bridge-host 192.168.1.50 --cmd-port 8890 --stat-port 8891
```

---

## 3. 界面

顶部状态栏 + 页签 + 底部信息栏，共 5 页：

| 页签 | 内容 |
| --- | --- |
| **充电总览** | 电池环形表（SOC/电压/充电电流/放电电流/温度）、充电状态大字、**真实充电流程时间轴**、本次充电统计、输出继电器操作、实时曲线（充电电流 + 电池电压 双 Y 轴）、无线充电状态 |
| **电源系统** | PMC 全部 13 项（总输入、头部/左右腿关节电机、12V 非隔离与隔离 DCDC、报警/紧急）+ 电池 5 项，电压电流带占比条 |
| **外设与灯光** | 红外 4 管与停车握手/落座指示灯、电源输出 VO1~4、拨码开关、继电器、风扇 4 路 PWM + 温度、LED 三通道与 RGB 原始数据 |
| **设备控制** | 5 个 Service 的操作面板，每个卡片「先改控件、再点下发」，下发结果显示在卡片底部 |
| **运行日志** | 带时间戳与分级配色的完整事件日志 |

顶部三颗状态胶囊分别显示：**链路状态 / 桥地址 / 桩状态**。
底部信息栏显示：桥地址、帧率、丢帧数、累计充电次数与电量、最近一条日志。

### 链路三态（排障关键）

界面能区分三种"没数据"的原因，不用猜：

| 胶囊显示 | 含义 | 该去查什么 |
| --- | --- | --- |
| `设备在线` | 正常收到状态帧 | — |
| `桥在线 · 设备无数据` | 桥活着，但设备没上报 | 串口是否插好、`mdc_node` 是否起来 |
| `桥未连接` | 连心跳都收不到 | `mdc_bridge` 是否在跑、端口/地址是否对 |

---

## 4. 目录结构

```
dual_charging_station/
├── shared/mdc_protocol.h        # ★ UDP JSON 协议契约（界面与桥共用）
├── dual_charging_station.pro    # qmake 工程（含 Qt Network）
├── main.cpp                     # 入口：主题、命令行参数、连接桥
├── theme.h / theme.cpp          # 全局配色 + 全局样式表（改这里即可换肤）
├── mdcdata.h                    # MdcData 数据结构（与 MdcData.msg 一一对应）+ JSON 解析
├── bridgeclient.h/.cpp          # UDP 通道：收发、链路三态、指令应答匹配、看门狗
├── stationcontroller.h/.cpp     # 状态派生 + 本地统计（替代旧 ChargerController）
├── mainwindow.h/.cpp            # 主窗口：页签外壳、顶部/底部、日志
├── pages/                       # 5 个页面
│   ├── pageutils.h              # 页面共用小工具（kvRow 等）
│   ├── overviewpage.h/.cpp      # 充电总览
│   ├── powerpage.h/.cpp         # 电源系统
│   ├── peripheralpage.h/.cpp    # 外设与灯光
│   └── controlpage.h/.cpp       # 设备控制
├── widgets/                     # 自绘控件
│   ├── card / metrictile        # 卡片容器、数据小格子
│   ├── circulargauge            # 环形电量表（属性动画 + 流动光点）
│   ├── chargecurvechart         # 双 Y 轴曲线（序列名可配置）
│   ├── stagetimeline            # 通用阶段时间轴
│   ├── lightindicator           # 指示灯行
│   ├── levelbar                 # 水平量条
│   └── bytegrid                 # uint8[] 十六进制方格
├── ros2_ws/                     # ROS2 工作区（colcon）
│   └── src/
│       ├── mdc_serial/          # 接口包（msg + 5 个 srv，本地复刻）
│       └── mdc_bridge/          # 桥节点 + 假设备
└── tools/run_demo.sh            # 一键启动
```

> `mainwindow.ui` 不再参与编译（界面为纯代码构建），文件保留备查。

---

## 5. 数据映射（MdcData → 界面）

`MdcData` 的字段**原样传输、不做换算**（电压 mV、电流 mA），只在显示时换算。

| 界面上的 | MdcData 字段 | 换算 |
| --- | --- | --- |
| 环形表 SOC | `battery_soc` | % |
| 电池总电压 | `battery_total_voltage` | mV ÷ 1000 |
| 充电 / 放电电流 | `battery_charge_current` / `battery_discharge_current` | mA ÷ 1000 |
| 电池温度 | `battery_temp` | ℃ |
| 充电功率 | —— | **本地计算** V × A |
| 充电中标志 | `ir_bs` | 1 = 充电中 |
| 停车握手 | `ir_sh` | 1 = 成功 |
| 机器人落座 | `robot_seated` | 1 = 已落座 |
| 输出继电器 | `powerio_relay1` / `relay2` | 0/1 |
| 报警 / 紧急 | `pmc_alarm_status` / `pmc_emergency_status` | 非 0 即异常 |
| 无线充电 | `wch_request_flag` / `busy` / `result` / `tx_data` / `rx_data` / `update_tick` | result: 0空闲 1处理中 2成功 3失败 |
| 关节电机 / 12V DCDC | `pmc_head_*` / `pmc_right_*` / `pmc_left_*` / `pmc_nonisolated_*` / `pmc_isolated_current` | mV / mA |
| 风扇 / LED / 拨码 | `fan*_pwm` / `fan_temp` / `led_*` / `powerio_sw*` | — |

### 协议里没有、由本机统计的量

| 界面上的 | 来源 | 说明 |
| --- | --- | --- |
| 本次已充电量 | 本地估算 | (当前 SOC − 本次起始 SOC) × 额定容量 |
| 已充时长 | 本地计时 | 充电期间累加 |
| 预计剩余 | 本地估算 | 剩余电量 ÷ 当前功率 |
| 累计充电次数 / 累计电量 | 本地累计 | 会话结束时累加，通过 `QSettings` 持久化 |

> 额定容量默认 1.92 kWh（48 V / 40 Ah），在 `StationController::m_packCapacityKwh`
> 调整，或通过 `QSettings` 的 `config/packCapacityKwh` 配置。
> 这些量在界面上都没有伪装成设备数据，请勿当成设备上报值使用。

### 真实充电流程（时间轴）

`MdcData` 里**没有**充电阶段字段，时间轴的 5 步是由若干状态位组合派生出来的：

```
停车检测(任一红外管) → 停车握手(ir_sh) → 机器人落座(robot_seated) → 充电中(ir_bs) → 充电完成
```

> 早期版本里的「恒流 CC / 恒压 CV / 涓流均衡」是**仿真臆造的阶段**，
> 真实协议中并不存在，已删除。柜内的 CC/CV 由充电模块自己管，不上报。

---

## 6. 设备控制（5 个 Service）

| 卡片 | 桥调用的服务 | 说明 |
| --- | --- | --- |
| 输出继电器 | `/mdc/relay_ctrl` | `relay1` / `relay2` 吸合或释放 |
| 无线充电 | `/mdc/wireless_charge` | 4 字节透传载荷，界面回显 `wch_result` |
| 风扇 | `/mdc/fan_ctrl` | 4 路 PWM 0~100 |
| 电源输出通道 | `/mdc/powerio_ctrl` | 位掩码 bit0~3 → VO1~4，**1=关 0=开** |
| LED 灯效 | `/mdc/led_ctrl` | 三通道模式与亮度 + RGB |

设计取向：**每个卡片都是「先改控件、再点下发」，不做即时下发**。
工控屏误触代价高，宁可多点一下也不要手滑切掉输出。应答超时 3 秒，
失败原因（服务不可用 / 超时 / 未知指令）直接显示在卡片底部。

> ⚠️ **待硬件确认**：文档没有说明哪一路继电器驱动充电输出。
> 本页只做透传控制，界面上已标注。请与硬件同事确认 `relay1` / `relay2` 的实际含义，
> 必要时把总览页的"闭合/断开输出继电器"按钮改成正确的组合。

---

## 7. 快捷键

| 快捷键 | 作用 |
| --- | --- |
| `F11` / `Esc` | 进入 / 退出全屏 |
| `Ctrl+1` ~ `Ctrl+5` | 切换页签 |
| `Ctrl+L` | 切换环回仿真模式（无桥时演示用） |

---

## 8. 接真机

1. 确认下位机侧 `mdc_node` 已运行：`ros2 topic hz /mdc_data` 应约 10 Hz；
2. 在**同一台机器**上启动桥：`ros2 run mdc_bridge mdc_bridge`；
3. 启动界面：`./build/dual_charging_station -f`（或 `tools/run_demo.sh --no-fake`）。

跨机部署时，桥与界面可以分别跑在两台机器上：

```bash
# 桥所在机器（默认监听 8890，向 127.0.0.1:8891 发状态）
ros2 run mdc_bridge mdc_bridge --ros-args -p peer_host:=192.168.1.77 -p peer_port:=8891
# 界面所在机器
./dual_charging_station --bridge-host 192.168.1.50
```

桥的参数（`--ros-args -p xxx:=yyy`）：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `cmd_port` | 8890 | 桥监听控制指令的端口 |
| `peer_host` / `peer_port` | 127.0.0.1 / 8891 | 状态帧发往哪里；界面先发过指令时会自动学习来源地址 |
| `topic` | `/mdc_data` | 订阅的话题名 |

### 本地复刻的接口包

`ros2_ws/src/mdc_serial` 是按 `doc/` 下的文档**本地复刻**的接口包，
用途是让没有机器人也能编译与验证。

- `msg/MdcData.msg` —— 照抄 `doc/MdcData.msg`，一致
- `srv/*.srv` —— 字段名与顺序依据 `doc/MDC接口使用指南.md` 的表格推断，
  **官方包如果有差异，以官方为准**；替换掉本目录后重新 `colcon build` 即可

---

## 9. 换肤与适配屏幕

- **改配色**：只改 `theme.h` 里的颜色常量，`theme.cpp` 的样式表用 `@TOKEN@`
  占位符自动套用，不会出现两处色值不一致。
- **改字号 / 圆角**：同样集中在 `theme.cpp` 的样式表里。
- **注意**：`CircularGauge` / `ChargeCurveChart` / `StageTimeline` 是自绘控件，
  **不受样式表影响**，颜色要通过 `setAccent()` 之类的 C++ 接口；它们的取色来自
  `theme.h` 常量，所以改 `theme.h` 同样能生效。
- **小屏适配**：三栏宽度用 `setMinimumWidth / setMaximumWidth`，会自动压缩；
  页面外层包了 `QScrollArea`，高度不够时可以滚动。窗口默认 1400×880，最小 1024×620。
- **无桌面环境的一体屏**：`QT_QPA_PLATFORM=linuxfb` 或 `eglfs` 直接启动即可。

```bash
# 例：1024x600 工控屏全屏运行
QT_QPA_PLATFORM=linuxfb ./dual_charging_station -f
```

### 工控板上的 libGL 报错（可忽略）

在 RK3588 这类板子上启动时，终端可能刷这几行：

```
libGL error: glx: failed to create dri3 screen
libGL error: failed to load driver: rockchip
```

这是 Qt 的 xcb 平台插件在探测 GLX 集成时，板卡厂商的 GPU 驱动不全导致的，
**与本程序无关，也不影响显示**：界面全部由 `QPainter` 光栅绘制，一行 OpenGL 都不用，
探测失败后 Qt 自动走软件路径，窗口照常显示。

程序启动时已经自动把 `QT_XCB_GL_INTEGRATION` 置为 `none`（在 `QApplication` 构造之前，
见 `main.cpp`），让 xcb 干脆不加载 GLX 集成插件，从根上不产生这些报错。
这个默认值只在外部没指定时才生效，需要用回 GLX 时用环境变量覆盖即可：

```bash
QT_XCB_GL_INTEGRATION=xcb_glx ./dual_charging_station
```

## 10. 持续集成（GitHub Actions）

两个工作流，各编一个架构，都是 Linux：

| 工作流文件 | 运行器 | 产物 |
| --- | --- | --- |
| `.github/workflows/build-x86_64.yml` | `ubuntu-22.04` | `dual_charging_station-linux-x86_64.tar.gz` |
| `.github/workflows/build-arm64.yml` | `ubuntu-22.04-arm` | `dual_charging_station-linux-arm64.tar.gz` |

**触发方式**：push 到 `master`，或在 Actions 页面手动触发（`workflow_dispatch`）。

**每个工作流做的事**：

1. 装 Qt5 + ROS2 Humble
2. `qmake` 编界面程序，`colcon` 编桥（`mdc_bridge` + `mdc_fake_pub`）
3. 校验产物架构与动态库（防止编成主机架构、防止缺库）
4. 跑冒烟测试 `tools/ci_smoke_test.sh`
5. 打包 tar.gz + sha256，上传到滚动 Release

**产物管理：不做版本管理。** 两份产物都挂在固定 tag `latest` 的同一个 Release 上，
同名文件直接覆盖（`gh release upload --clobber`），所以 Releases 页面永远只有一份最新的，
下载地址固定不变，不保留历史版本。两个架构的文件名不同，各覆盖各的；两个工作流
用同一个 `concurrency` 组串行执行，避免同时改 Release 打架。

**为什么两个架构都用 22.04**：ROS2 Humble 官方只支持 Ubuntu 22.04；更重要的是 glibc 基线 ——
在 24.04（glibc 2.39）上编出来的二进制，拿到跑 22.04（glibc 2.35）的一体屏上会直接报
`GLIBC_2.38 not found`。arm64 用的是 GitHub 的 arm64 原生运行器（`ubuntu-22.04-arm`，
公开仓库免费不限量），是真 aarch64 机器而不是 QEMU 模拟，所以编译速度和 x86 一样正常。

### 冒烟测试 `tools/ci_smoke_test.sh`

CI 里不只验证"编过了"，而是真的跑一遍。本地也能跑，用来复现 CI 的检查：

```bash
tools/ci_smoke_test.sh
```

三个阶段，任一失败即返回非 0：

| 阶段 | 做什么 | 证明了什么 |
| --- | --- | --- |
| 1 | 无头启动界面（环回模式，跑 8 秒） | 二进制能启动、5 个页面都能构造、事件循环在跑 |
| 2 | 假设备 → 桥 → UDP → 界面 | 界面确实收到了 10Hz 数据流，而不是"只是没崩" |
| 3 | 模拟界面下发 5 条控制指令 | UDP → 桥 → ROS2 服务 这一向也通，假设备收到了正确参数 |

无头运行靠 `QT_QPA_PLATFORM=offscreen`（CI 里没有显示器）。界面新增了 `--verbose`
参数，把链路状态与帧数打到标准输出，脚本据此判断"真的收到数据了"：

```bash
QT_QPA_PLATFORM=offscreen ./dual_charging_station --verbose --bridge-host 127.0.0.1
# [链路] 在线
# [统计] 帧=119 速率=10Hz 丢帧=0 链路=在线 电量=39%
```

这个参数在现场排障时同样有用：一体屏上没调试器，把输出重定向到文件就能事后追溯。

