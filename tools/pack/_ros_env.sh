#!/usr/bin/env bash
#
# 被 run_bridge.sh / run_fake.sh / run_demo.sh 共用：准备好 ROS2 运行环境。
#
# 用法（必须先设好 PKG_ROOT）：
#     PKG_ROOT=/path/to/package source _ros_env.sh
#
# 做两件事：
#   1. source 目标机的 /opt/ros/<distro>/setup.bash
#      —— 桥是 ROS2 节点，没有 ROS2 环境跑不起来
#   2. 把产物自带的 lib/ 加到 LD_LIBRARY_PATH
#      —— mdc_serial 的接口库随包一起发，桥运行时要 dlopen 它们

ROS_DISTRO="${ROS_DISTRO:-humble}"

if [[ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]]; then
    {
        echo "找不到 /opt/ros/${ROS_DISTRO}/setup.bash"
        echo
        echo "mdc_bridge / mdc_fake_pub 是 ROS2 节点，目标机必须装上 ROS2 ${ROS_DISTRO}。"
        echo "    sudo apt install ros-${ROS_DISTRO}-ros-base"
        echo
        echo "如果只想跑显示屏界面，请用 run_ui.sh —— 它不依赖 ROS2。"
        echo "（想换发行版可以设 ROS_DISTRO 环境变量）"
    } >&2
    exit 1
fi

# ROS 的 setup.bash 第 8 行就引用了未定义变量 AMENT_TRACE_SETUP_FILES，
# 开着 -u（nounset）去 source 它必然报 unbound variable 直接退出，
# 所以只在 source 期间临时关掉 -u。
set +u
# shellcheck disable=SC1090
source "/opt/ros/${ROS_DISTRO}/setup.bash"
set -u

export LD_LIBRARY_PATH="${PKG_ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

# ---------------------------------------------------------------- 域 ID
# ROS2 的域是隔离的：域不同的两个节点，**在同一台机器上也互相看不见**，
# 而且没有任何报错——现象就是"桥明明跑着，界面一帧都收不到"。
# 接口文档规定用 122，这里默认对齐；接别的域自己 export 覆盖。
ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-122}"
export ROS_DOMAIN_ID
echo "ROS_DOMAIN_ID = ${ROS_DOMAIN_ID}（默认 122，可用环境变量覆盖）"

# ---------------------------------------------------------------- 日志目录
# rclcpp 默认写 $HOME/.ros/log，一体屏上程序往往由开机脚本 / systemd 拉起，
# $HOME 是多少不好说，现场翻日志得先猜路径。统一收到包目录下的 log/。
# （直接跑 bin/mdc_bridge 而没经过脚本时，程序自己会把目录指到
#   "可执行文件所在目录/log"，见 src/run_log_dir.h）
ROS_LOG_DIR="${ROS_LOG_DIR:-${PKG_ROOT}/log}"
export ROS_LOG_DIR
echo "日志目录   = ${ROS_LOG_DIR}"
