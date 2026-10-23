#!/usr/bin/env bash
#
# ============================================================================
#  make_package.sh —— 把编译产物打成可直接部署的压缩包
# ============================================================================
#
#  CI 的两个工作流都调这个脚本，所以本地也能一条命令验证打包结果 ——
#  上一次就是因为没能在本地验证打包，漏掉了 mdc_serial 的接口库，
#  导致板子上跑 mdc_bridge 报 libmdc_serial__rosidl_typesupport_cpp.so 找不到。
#
#  用法：
#      tools/make_package.sh dual_charging_station-linux-x86_64
#
#  产物布局（<包名>/）：
#      bin/chric_software_dual_charging_station   显示屏界面（不依赖 ROS2，直接能跑）
#      bin/chric_software_mdc_bridge              ROS2 桥（目标机需装 ROS2）
#      bin/chric_software_mdc_fake_pub            假设备
#      lib/libmdc_serial__*.so     mdc_serial 接口库，桥运行时 dlopen 它们
#      run_ui.sh / run_bridge.sh / run_fake.sh / run_demo.sh / _ros_env.sh
#      README.md / 构建信息.txt
#
#  前置条件：界面已 qmake 编译、ros2_ws 已 colcon 编译。
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG_NAME="${1:-}"
ROS_DISTRO="${ROS_DISTRO:-humble}"

if [[ -z "$PKG_NAME" ]]; then
    echo "用法: tools/make_package.sh <包名>" >&2
    exit 1
fi

APP="$ROOT/build/chric_software_dual_charging_station"
BRIDGE_DIR="$ROOT/ros2_ws/install/mdc_bridge/lib/mdc_bridge"
SERIAL_LIB_DIR="$ROOT/ros2_ws/install/mdc_serial/lib"

# ---------------------------------------------------------------- 前置检查
for f in "$APP" "$BRIDGE_DIR/chric_software_mdc_bridge" "$BRIDGE_DIR/chric_software_mdc_fake_pub"; do
    if [[ ! -f "$f" ]]; then
        echo "缺少 $f —— 先把界面和 ros2_ws 都编译好再打包" >&2
        exit 1
    fi
done
if [[ ! -d "$SERIAL_LIB_DIR" ]]; then
    echo "缺少 $SERIAL_LIB_DIR —— 先 colcon build 出 mdc_serial" >&2
    exit 1
fi

# ---------------------------------------------------------------- 组装
STAGE="$ROOT/dist/$PKG_NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib"

cp "$APP" "$STAGE/bin/chric_software_dual_charging_station"
cp "$BRIDGE_DIR/chric_software_mdc_bridge"   "$STAGE/bin/"
cp "$BRIDGE_DIR/chric_software_mdc_fake_pub" "$STAGE/bin/"

# mdc_serial 的接口库必须整套带上：
# 桥只**直接**依赖 libmdc_serial__rosidl_typesupport_cpp.so 这一个（ldd 能看见），
# 但 fastrtps / introspection / generator_c 那几个是运行时 dlopen 的，ldd 看不见。
# 少任何一个，目标机上都会在启动或首次调用服务时报 cannot open shared object file。
cp "$SERIAL_LIB_DIR"/libmdc_serial__*.so* "$STAGE/lib/"

cp "$ROOT"/tools/pack/*.sh "$STAGE/"
cp "$ROOT"/tools/pack/*.service.in "$STAGE/"
chmod +x "$STAGE"/*.sh
cp "$ROOT/README.md" "$STAGE/"

# 去掉调试符号，一体屏上体积小一点
strip "$STAGE"/bin/* 2>/dev/null || true
strip "$STAGE"/lib/*.so* 2>/dev/null || true

# ---------------------------------------------------------------- 产物说明
{
    echo "产物      : ${PKG_NAME}"
    echo "架构      : $(uname -m)"
    echo "系统      : $(. /etc/os-release && echo "$PRETTY_NAME")"
    echo "提交      : ${GITHUB_SHA:-（本地构建）}"
    echo "构建时间  : $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
    echo "Qt 版本   : $(qmake -query QT_VERSION 2>/dev/null || echo '未知')"
    echo "编译器    : $(g++ --version | head -n 1)"
    echo
    echo "内容："
    echo "  bin/chric_software_dual_charging_station   显示屏界面（不依赖 ROS2，直接能跑）"
    echo "  bin/chric_software_mdc_bridge              ROS2 桥（目标机需装 ROS2 ${ROS_DISTRO}）"
    echo "  bin/chric_software_mdc_fake_pub            假设备，无真机时验证链路用"
    echo "  lib/                        mdc_serial 接口库，chric_software_mdc_bridge 运行时需要"
    echo "  run_ui.sh                   起界面（不依赖 ROS2）"
    echo "  run_bridge.sh               起桥（自动 source ROS 并设好库路径）"
    echo "  run_fake.sh                 起假设备"
    echo "  run_demo.sh                 一键起全套：假设备 + 桥 + 界面"
    echo "  install_service.sh          注册成 systemd 服务（开机自启 + 崩溃重启）"
    echo
    echo "目标机依赖："
    echo "  【只跑界面】"
    echo "      sudo apt install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5"
    echo "      起不来先查： ldd ./bin/chric_software_dual_charging_station | grep 'not found'"
    echo "      若报 libgssapi.so.3 —— 是板子上的 Qt5 用的是 Heimdal 版 GSSAPI，"
    echo "      与我们打包用的 krb5 版不同（不是漏打包），装 libgssapi3-heimdal 即可。"
    echo "  【要跑桥 / 假设备】额外还需要 ROS2 ${ROS_DISTRO}，且必须是同样的发行版："
    echo "      sudo apt install ros-${ROS_DISTRO}-ros-base"
    echo "      确认 /opt/ros/${ROS_DISTRO}/setup.bash 存在"
    echo "      桥是 ROS2 节点，没有 ROS2 直接跑不起来；界面不受影响。"
    echo
    echo "快速上手（拷到目标机后）："
    echo "  tar -xzf ${PKG_NAME}.tar.gz && cd ${PKG_NAME}"
    echo "  ./run_demo.sh             # 无真机：假设备 + 桥 + 界面，整条链路"
    echo "  ./run_ui.sh --loopback    # 只要界面，用内置仿真数据"
    echo "  ./run_bridge.sh           # 只起桥（真机 mdc_node 已在运行）"
    echo "  sudo ./install_service.sh # 注册成开机自启（推荐；界面+桥）"
    echo
    echo "注：lib/ 里是依据 doc/ 复刻的 mdc_serial 接口库。机器人上若装有官方"
    echo "    包，请以官方包为准，重新 colcon build 后替换。"
} > "$STAGE/构建信息.txt"

# ---------------------------------------------------------------- 打包
tar -czf "$ROOT/dist/${PKG_NAME}.tar.gz" -C "$ROOT/dist" "$PKG_NAME"
cd "$ROOT/dist"
sha256sum "${PKG_NAME}.tar.gz" > "${PKG_NAME}.tar.gz.sha256"

echo "打包完成："
ls -lh "${PKG_NAME}.tar.gz" "${PKG_NAME}.tar.gz.sha256"
echo "解包后体积：$(du -sh "$PKG_NAME" | cut -f1)"
