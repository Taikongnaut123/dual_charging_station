#!/usr/bin/env bash
#
# 启动充电桩显示屏界面。
#
# **不依赖 ROS2** —— 可以直接跑在没装 ROS2 的一体屏上，只要装齐 Qt5 运行库。
#
# 用法（参数原样交给界面程序，完整参数见 README）：
#     ./run_ui.sh                   # 监听本机 UDP 8891（桥默认往这里发）
#     ./run_ui.sh -f                # 全屏（一体屏通常这么跑）
#     ./run_ui.sh --loopback        # 内置仿真数据，连桥和 ROS2 都不需要
#     ./run_ui.sh --stat-port 9001  # 换接收端口（桥那边要配 peer_port）
#     ./run_ui.sh --verbose         # 把链路状态与帧数打到终端
#
set -euo pipefail

PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "${PKG_ROOT}/bin/dual_charging_station" "$@"
