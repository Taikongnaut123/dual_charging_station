#!/usr/bin/env bash
#
# 把显示屏程序注册成 systemd 服务：开机自启 + 崩溃自动拉起。
#
# 用法（需要 root）：
#     sudo ./install_service.sh                # 界面 + 桥（推荐）
#     sudo ./install_service.sh --ui-only      # 只起界面（桥在别处跑）
#     sudo ./install_service.sh --bridge-only  # 只起桥（界面另开）
#     sudo ./install_service.sh --uninstall    # 卸载
#     ./install_service.sh --print             # 不安装，只打印生成的 unit
#
# 为什么要用 systemd 而不是 ~/.xinitrc：面板是"设备"，不是"桌面"。
# systemd 能保证崩溃后自动重启、开机就有、日志进 journal。
#
set -euo pipefail

PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNIT_NAME="chric-software-dual-charging-station"
UNIT_PATH="/etc/systemd/system/${UNIT_NAME}.service"
TEMPLATE="${PKG_ROOT}/${UNIT_NAME}.service.in"

# 改名前的服务名。装新服务时必须把它停掉，否则开机两套一起拉，
# 界面抢 UDP 8891（后起的会以退出码 2 退出，但日志会很难看）。
LEGACY_UNIT_NAME="dual-charging-station"
LEGACY_UNIT_PATH="/etc/systemd/system/${LEGACY_UNIT_NAME}.service"

MODE="all"
PRINT_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --ui-only)     MODE="ui" ;;
        --bridge-only) MODE="bridge" ;;
        --uninstall)   MODE="uninstall" ;;
        --print)       PRINT_ONLY=1 ;;
        -h|--help)     sed -n '2,16p' "$0"; exit 0 ;;
        *)             echo "未知参数: $arg（-h 查看用法）" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------- 生成 unit
render_unit()
{
    local exec_line
    case "$MODE" in
        ui)
            [[ -x "${PKG_ROOT}/run_ui.sh" ]] || { echo "找不到 ${PKG_ROOT}/run_ui.sh" >&2; exit 1; }
            exec_line="${PKG_ROOT}/run_ui.sh -f"
            ;;
        bridge)
            [[ -x "${PKG_ROOT}/run_bridge.sh" ]] || { echo "找不到 ${PKG_ROOT}/run_bridge.sh" >&2; exit 1; }
            exec_line="${PKG_ROOT}/run_bridge.sh"
            ;;
        *)
            # 界面 + 桥：--no-fake 表示不启动假设备，接真机 mdc_node
            [[ -x "${PKG_ROOT}/run_demo.sh" ]] || { echo "找不到 ${PKG_ROOT}/run_demo.sh" >&2; exit 1; }
            exec_line="${PKG_ROOT}/run_demo.sh --no-fake"
            ;;
    esac

    [[ -f "$TEMPLATE" ]] || { echo "找不到模板 ${TEMPLATE}" >&2; exit 1; }

    # 路径里可能含 / & | 等 sed 元字符，用 | 作分隔符并转义 &
    local esc_root=${PKG_ROOT//&/\\&}
    local esc_exec=${exec_line//&/\\&}
    sed -e "s|@PKG_ROOT@|${esc_root}|g" -e "s|@EXEC_LINE@|${esc_exec}|g" "$TEMPLATE"
}

if [[ $PRINT_ONLY -eq 1 ]]; then
    render_unit
    exit 0
fi

if [[ $EUID -ne 0 ]]; then
    echo "需要 root 权限（要写 /etc/systemd/system）：" >&2
    echo "    sudo $0 $*" >&2
    echo "只想看看会生成什么，可以先跑： $0 --print" >&2
    exit 1
fi

# ---------------------------------------------------------------- 卸载
if [[ "$MODE" == "uninstall" ]]; then
    systemctl stop "$UNIT_NAME" 2>/dev/null || true
    systemctl disable "$UNIT_NAME" 2>/dev/null || true
    rm -f "$UNIT_PATH"
    # 旧名字的服务一并清掉，避免“卸了好像又没卸”
    if [[ -f "$LEGACY_UNIT_PATH" ]]; then
        systemctl stop "$LEGACY_UNIT_NAME" 2>/dev/null || true
        systemctl disable "$LEGACY_UNIT_NAME" 2>/dev/null || true
        rm -f "$LEGACY_UNIT_PATH"
        echo "已连带移除旧服务 ${LEGACY_UNIT_NAME}"
    fi
    systemctl daemon-reload
    echo "已卸载 ${UNIT_NAME}（程序本身没动，还在 ${PKG_ROOT}）"
    exit 0
fi

# ---------------------------------------------------------------- 安装
echo "包目录   : ${PKG_ROOT}"
echo "服务名   : ${UNIT_NAME}"
echo "运行方式 : $([[ "$MODE" == ui ]] && echo '只起界面' || { [[ "$MODE" == bridge ]] && echo '只起桥' || echo '界面 + 桥'; })"
echo

# 改名前的旧服务先停掉，否则两套会抢同一个 UDP 端口
if [[ -f "$LEGACY_UNIT_PATH" ]]; then
    echo "发现旧服务 ${LEGACY_UNIT_NAME}，正在停用并移除 ..."
    systemctl stop "$LEGACY_UNIT_NAME" 2>/dev/null || true
    systemctl disable "$LEGACY_UNIT_NAME" 2>/dev/null || true
    rm -f "$LEGACY_UNIT_PATH"
    systemctl daemon-reload
    echo
fi

render_unit > "$UNIT_PATH"
chmod 644 "$UNIT_PATH"

systemctl daemon-reload
systemctl enable "$UNIT_NAME" >/dev/null
systemctl restart "$UNIT_NAME"

sleep 2
if systemctl is-active --quiet "$UNIT_NAME"; then
    echo "已启动并设为开机自启。"
else
    echo "服务没起来，看日志：" >&2
    echo "    journalctl -u ${UNIT_NAME} -n 50 --no-pager" >&2
    exit 1
fi

cat <<EOF

常用命令：
    systemctl status ${UNIT_NAME}          # 看状态
    systemctl restart ${UNIT_NAME}         # 重启（改完配置/换热插）
    systemctl stop ${UNIT_NAME}            # 暂停（屏幕会黑）
    journalctl -u ${UNIT_NAME} -f          # 实时日志
    sudo ./install_service.sh --uninstall  # 卸载

注意：
  - 程序崩了 systemd 会每 3 秒重新拉起（Restart=always）
  - 日志在 journal 里，桥自己还会往 ${PKG_ROOT}/log/ 写文件
EOF
