#!/usr/bin/env bash
#
# 双轮足机器人充电桩 —— 一键启动
#
#   mdc_fake_pub（假设备） ─→ mdc_bridge（ROS2 桥） ─→ 显示屏界面（本程序）
#                                ↑
#                       真机时换成 mdc_node
#
# 用法：
#   tools/run_demo.sh                # 假设备 + 桥 + 界面（全链路演示）
#   tools/run_demo.sh --no-fake      # 只起桥 + 界面（接真机 mdc_node 时用）
#   tools/run_demo.sh --loopback     # 只起界面，用内置仿真数据（不依赖 ROS2）
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS="$ROOT/ros2_ws"
APP="$ROOT/build/chric_software_dual_charging_station"
BRIDGE_DIR="$WS/install/mdc_bridge/lib/mdc_bridge"

USE_FAKE=1
LOOPBACK=0
for arg in "$@"; do
  case "$arg" in
    --no-fake)  USE_FAKE=0 ;;
    --loopback) LOOPBACK=1 ;;
    -h|--help)  sed -n '2,15p' "$0"; exit 0 ;;
    *)          echo "未知参数: $arg（-h 查看用法）" >&2; exit 1 ;;
  esac
done

if [[ ! -x "$APP" ]]; then
  echo "找不到 $APP" >&2
  echo "请先构建界面程序： mkdir -p build && cd build && qmake .. && make -j\$(nproc)" >&2
  exit 1
fi

if [[ $LOOPBACK -eq 1 ]]; then
  echo "以环回仿真模式启动界面（不连接 ROS2）"
  exec "$APP" --loopback -f
fi

if [[ ! -f "$WS/install/setup.bash" ]]; then
  echo "找不到 $WS/install/setup.bash" >&2
  echo "请先构建桥： source /opt/ros/humble/setup.bash && cd ros2_ws && colcon build" >&2
  exit 1
fi

# shellcheck disable=SC1091
source /opt/ros/humble/setup.bash
# shellcheck disable=SC1091
source "$WS/install/setup.bash"

# 域 ID 对不上时本机演示同样收不到数据（域是隔离的），这里跟着文档默认 122
ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-122}"
export ROS_DOMAIN_ID
echo "ROS_DOMAIN_ID = ${ROS_DOMAIN_ID}"

PIDS=()
cleanup() {
  echo
  echo "正在退出 ..."
  for pid in "${PIDS[@]}"; do
    kill "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [[ $USE_FAKE -eq 1 ]]; then
  echo "[1/3] 启动假设备 chric_software_mdc_fake_pub ..."
  "$BRIDGE_DIR/chric_software_mdc_fake_pub" &
  PIDS+=($!)
  sleep 1
else
  echo "[1/3] 跳过假设备（假定真机 mdc_node 已在运行）"
fi

echo "[2/3] 启动 ROS2 桥 chric_software_mdc_bridge ..."
"$BRIDGE_DIR/chric_software_mdc_bridge" &
PIDS+=($!)
sleep 1

echo "[3/3] 启动显示屏界面（全屏）..."
"$APP" -f &
PIDS+=($!)

echo
echo "全部就绪。按 Ctrl+C 退出（会一并关掉桥与假设备）。"
wait
