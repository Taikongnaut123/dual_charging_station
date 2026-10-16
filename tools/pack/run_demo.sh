#!/usr/bin/env bash
#
# 一键起全套：假设备 + 桥 + 界面。没有真机时用来验证整条链路。
#
#     ./run_demo.sh             全链路（假设备 + 桥 + 界面）
#     ./run_demo.sh --no-fake   只起桥 + 界面（真机 mdc_node 已在运行时用）
#     ./run_demo.sh --loopback  只起界面，用内置仿真数据（连 ROS2 都不需要）
#
# 数据链路：
#     mdc_fake_pub（假设备） ─→ mdc_bridge（ROS2 桥） ─→ 显示屏界面
#                                   ↑
#                          真机时换成 mdc_node
#
set -euo pipefail

PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${PKG_ROOT}/bin/dual_charging_station"

USE_FAKE=1
LOOPBACK=0
for arg in "$@"; do
    case "$arg" in
        --no-fake)  USE_FAKE=0 ;;
        --loopback) LOOPBACK=1 ;;
        -h|--help)  sed -n '2,13p' "$0"; exit 0 ;;
        *)          echo "未知参数: $arg（-h 查看用法）" >&2; exit 1 ;;
    esac
done

if [[ $LOOPBACK -eq 1 ]]; then
    echo "以环回仿真模式启动界面（不连接 ROS2）"
    exec "$APP" --loopback -f
fi

# shellcheck source=_ros_env.sh
source "${PKG_ROOT}/_ros_env.sh"

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
    echo "[1/3] 启动假设备 mdc_fake_pub ..."
    "${PKG_ROOT}/bin/mdc_fake_pub" &
    PIDS+=($!)
    sleep 1
else
    echo "[1/3] 跳过假设备（假定真机 mdc_node 已在运行）"
fi

echo "[2/3] 启动 ROS2 桥 mdc_bridge ..."
"${PKG_ROOT}/bin/mdc_bridge" &
PIDS+=($!)
sleep 1

echo "[3/3] 启动显示屏界面（全屏）..."
"$APP" -f &
PIDS+=($!)

echo
echo "全部就绪。按 Ctrl+C 退出（会一并关掉桥与假设备）。"
wait
