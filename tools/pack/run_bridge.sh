#!/usr/bin/env bash
#
# 启动 ROS2 桥 mdc_bridge。
#
# 需要目标机已装 ROS2（默认 humble，可用 ROS_DISTRO 环境变量改）。
# 脚本会自动 source ROS 环境并把产物自带的 lib/ 加进库搜索路径。
#
# 用法（参数原样交给桥，例如指定桥的地址）：
#     ./run_bridge.sh
#     ./run_bridge.sh --ros-args -p peer_host:=192.168.1.50 -p log_level:=debug
#
set -euo pipefail

PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=_ros_env.sh
source "${PKG_ROOT}/_ros_env.sh"

exec "${PKG_ROOT}/bin/chric_software_mdc_bridge" "$@"
