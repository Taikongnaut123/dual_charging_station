#!/usr/bin/env bash
#
# 启动充电桩显示屏界面。
#
# **不依赖 ROS2** —— 可以直接跑在没装 ROS2 的一体屏上，只要装齐 Qt5 运行库。
#
# 用法（参数原样交给界面程序，完整参数见 README）：
#     ./run_ui.sh                            # 连本机 127.0.0.1 上的桥
#     ./run_ui.sh --bridge-host 192.168.1.50 # 桥在别的机器上
#     ./run_ui.sh --loopback                 # 内置仿真数据，连桥都不需要
#     ./run_ui.sh -f                         # 全屏（一体屏通常这么跑）
#     ./run_ui.sh --verbose                  # 把链路状态与帧数打到终端
#
set -euo pipefail

PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "${PKG_ROOT}/bin/dual_charging_station" "$@"
