#!/usr/bin/env bash
#
# ============================================================================
#  ci_smoke_test.sh —— 冒烟测试：不只验证"编过了"，而是验证"真的跑得起来"
# ============================================================================
#
#  三个阶段，任一失败 → 退出码 1：
#
#    阶段 1  无头启动界面（环回模式）
#            证明：二进制能启动、三个页面都能构造、事件循环在跑
#
#    阶段 2  假设备 → 桥 → UDP → 界面  全链路
#            证明：界面确实收到了约 2Hz 的真实数据流，而不是"只是没崩"
#
#    阶段 3  协议字段完整性校验
#            另起一个桥把状态帧发到 8892，用 python 逐项核对
#            shared/mdc_protocol.h 里定义的键是否一个不少、一个不多
#            —— 能抓住"加了字段但桥上忘了序列化"这类静默 bug
#
#  协议是**只收不发**（充电桩主机自主完成充电控制），所以没有下发类测试。
#
#  无头运行靠 QT_QPA_PLATFORM=offscreen —— CI 里没有显示器，一体屏上也没有
#  调试器，所以这个开关同时也是现场排障用的。
#
#  ── 用法（本地也能跑，用来复现 CI 的检查）──
#     source /opt/ros/humble/setup.bash
#     cd ros2_ws && colcon build && cd ..
#     mkdir -p build && cd build && qmake .. && make -j$(nproc) && cd ..
#     tools/ci_smoke_test.sh
#
#  ── 环境变量 ──
#     ROS_DISTRO     ROS2 发行版，默认 humble
#     LOG_DIR        日志目录，默认 build/ci-logs
#     APP_BIN        界面程序路径（默认 build/dual_charging_station）
#     BRIDGE_BIN     桥路径（默认 ros2_ws/install 里那个）
#     FAKE_BIN       假设备路径（同上）
#     OVERLAY_SETUP  本工程 colcon install 的 setup.bash；显式设为空字符串则跳过，
#                    用来验证「打包产物 + 系统 ROS2」这种目标机上的真实情形
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS="$ROOT/ros2_ws"
ROS_DISTRO="${ROS_DISTRO:-humble}"
LOG_DIR="${LOG_DIR:-$ROOT/build/ci-logs}"

# 三个可执行文件的路径都可用环境变量覆盖：
# CI 里会指向解包后的产物目录，以此验证产物能脱离本工程的构建树独立运行。
APP="${APP_BIN:-$ROOT/build/dual_charging_station}"
BRIDGE="${BRIDGE_BIN:-$WS/install/mdc_bridge/lib/mdc_bridge/mdc_bridge}"
FAKE="${FAKE_BIN:-$WS/install/mdc_bridge/lib/mdc_bridge/mdc_fake_pub}"
# 注意用 ${VAR-默认} 而不是 ${VAR:-默认}：显式传空字符串表示"不要 source overlay"
OVERLAY_SETUP="${OVERLAY_SETUP-$WS/install/setup.bash}"

# 界面监听状态帧的端口（与 shared/mdc_protocol.h 的 kDefaultStatPort 一致）
# 可被环境变量覆盖：本机 8891 被别的实例占着时用 STAT_PORT=8901 跑
STAT_PORT="${STAT_PORT:-8891}"
# 阶段 3 用的独立端口，避免和界面抢 8891
PROBE_PORT="${PROBE_PORT:-8892}"

export QT_QPA_PLATFORM=offscreen
mkdir -p "$LOG_DIR"

RED=$'\033[1;31m'; GREEN=$'\033[1;32m'; CYAN=$'\033[1;36m'; YELLOW=$'\033[1;33m'; NC=$'\033[0m'

FAILED=0
step()  { printf '\n%s==> %s%s\n' "$CYAN" "$*" "$NC"; }
pass()  { printf '%s  [通过]%s %s\n' "$GREEN" "$NC" "$*"; }
fail()  { printf '%s  [失败]%s %s\n' "$RED" "$NC" "$*"; FAILED=1; }
info()  { printf '        %s\n' "$*"; }
dump()  { printf '%s----- %s -----%s\n' "$YELLOW" "$1" "$NC"; tail -n 25 "$1" 2>/dev/null || echo "(没有日志)"; }

# ============================================================ 前置检查
step "前置检查"

for f in "$APP" "$BRIDGE" "$FAKE"; do
  if [[ -x "$f" ]]; then
    pass "找到可执行文件 $f"
  else
    fail "缺少可执行文件 $f"
  fi
done

if [[ $FAILED -ne 0 ]]; then
  echo
  echo "构建产物不全。先编译界面程序与 ROS2 桥，再跑冒烟测试。"
  exit 1
fi

info "架构        : $(uname -m)"
info "OS          : $(. /etc/os-release && echo "$PRETTY_NAME")"
info "无头平台插件: QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
info "日志目录    : $LOG_DIR"

PIDS=()
cleanup() {
  local pid
  for pid in "${PIDS[@]}"; do
    kill "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# ============================================================ 阶段 1
step "阶段 1／3　无头启动界面（环回模式，跑 8 秒）"

P1="$LOG_DIR/phase1-app.log"

# timeout 把界面正常终止时返回 124 —— 跑满 8 秒没退出 = 健康
timeout -s TERM 8 "$APP" --loopback --verbose >"$P1" 2>&1
rc=$?

if [[ $rc -eq 124 ]]; then
  pass "界面连续运行 8 秒未退出（被 timeout 正常终止，退出码 124）"
else
  fail "界面提前退出了，退出码 $rc（0 = 自己退了，134 = 崩了）"
  dump "$P1"
fi

if grep -q '\[统计\]' "$P1"; then
  pass "事件循环在运行，链路统计正在输出："
  grep '\[统计\]' "$P1" | tail -n 1 | sed 's/^/        /'
else
  fail "没有看到 [统计] 输出，界面事件循环可能没跑起来"
fi

if grep -qiE 'segmentation fault|core dumped|Aborted|Assertion .* failed' "$P1"; then
  fail "日志里出现崩溃迹象"
  dump "$P1"
else
  pass "日志里没有崩溃迹象"
fi

# ============================================================ 阶段 2
step "阶段 2／3　全链路：假设备 → 桥 → UDP → 界面"

P2F="$LOG_DIR/phase2-fake.log"
P2B="$LOG_DIR/phase2-bridge.log"
P2A="$LOG_DIR/phase2-app.log"

# ROS 的 setup.bash 里有未定义变量，必须临时关掉 -u 才能 source
set +u
# shellcheck disable=SC1090
source "/opt/ros/$ROS_DISTRO/setup.bash"
if [[ -n "$OVERLAY_SETUP" ]]; then
  # shellcheck disable=SC1090
  source "$OVERLAY_SETUP"
else
  info "跳过本工程 overlay，只用系统 ROS2（模拟目标机上的情形）"
fi
set -u

"$FAKE"   >"$P2F" 2>&1 & PIDS+=($!)
sleep 1
"$BRIDGE" --ros-args -p peer_port:="$STAT_PORT" >"$P2B" 2>&1 & PIDS+=($!)
sleep 2
"$APP" --verbose --stat-port "$STAT_PORT" >"$P2A" 2>&1 & PIDS+=($!)

info "三个进程已启动（界面监听 UDP ${STAT_PORT}），等 12 秒看链路是否打通 ..."
sleep 12

# 关键：界面绑不上端口时会**退回环回模式**，而环回模式自己按 2Hz 造反数据，
# 帧数和链路状态都会“看起来很健康”——测试会假通过。必须先排除这种情况。
if grep -qE '无法绑定 UDP|已进入环回模式' "$P2A"; then
  fail "界面没能绑定 UDP ${STAT_PORT}，已退回环回仿真模式（端口被其它实例占用？）——本次结果不可信"
  dump "$P2A"
fi

if grep -q '桥已启动' "$P2B"; then
  pass "桥进程启动正常"
else
  fail "桥没有正常启动"
  dump "$P2B"
fi

# 界面到底有没有收到数据？从它自己输出的 [统计] 行里读帧数和速率
LAST_STAT="$(grep '\[统计\]' "$P2A" | tail -n 1 || true)"

if [[ -z "$LAST_STAT" ]]; then
  fail "界面没有任何 [统计] 输出，说明一帧数据都没收到"
  dump "$P2A"
  dump "$P2B"
else
  info "界面最后一条统计：$LAST_STAT"
  # 必须从行首锚定：用贪婪的 .*帧= 会先匹配到后面的「丢帧=0」，
  # 取出来的帧数永远是 0，看起来就像链路没通（踩过这个坑）。
  FRAMES="$(sed -n 's/^\[统计\] 帧=\([0-9]*\).*/\1/p' <<<"$LAST_STAT")"
  RATE="$(sed -n 's/^\[统计\] .*速率=\([0-9]*\)Hz.*/\1/p' <<<"$LAST_STAT")"

  # 设备约 2Hz，12 秒理论上是 24 帧；留足余量
  if [[ "${FRAMES:-0}" -ge 10 ]]; then
    pass "界面 12 秒内收到 ${FRAMES} 帧（设备约 2Hz，阈值 ≥10 帧通过）"
  else
    fail "界面只收到 ${FRAMES:-0} 帧，链路没打通"
    dump "$P2A"
    dump "$P2B"
  fi

  if [[ "${RATE:-0}" -ge 1 ]]; then
    pass "帧率 ${RATE}Hz（阈值 ≥1Hz 通过）"
  else
    fail "帧率只有 ${RATE:-0}Hz，数据不连续"
  fi

  if grep -q '链路=在线' <<<"$LAST_STAT"; then
    pass "界面链路三态判定 = 在线"
  else
    fail "界面链路状态不是「在线」：$LAST_STAT"
  fi

  # 数据真的被解析了吗？SOC 应该在 1~100，状态文本应该是已知取值
  MSOC="$(sed -n 's/^.*手动SOC=\([0-9]*\)%.*/\1/p' <<<"$LAST_STAT")"
  ASOC="$(sed -n 's/^.*自动SOC=\([0-9]*\)%.*/\1/p' <<<"$LAST_STAT")"
  if [[ "${MSOC:-0}" -ge 1 && "${MSOC:-0}" -le 100 && "${ASOC:-0}" -ge 1 && "${ASOC:-0}" -le 100 ]]; then
    pass "SOC 解析正常（手动 ${MSOC}% / 自动 ${ASOC}%）"
  else
    fail "SOC 数值不合理（手动 ${MSOC:-空} / 自动 ${ASOC:-空}），解析可能有问题"
    dump "$P2A"
  fi
fi

# ============================================================ 阶段 3
step "阶段 3／3　协议字段完整性校验（对照 shared/mdc_protocol.h）"

P3="$LOG_DIR/phase3-probe.log"
P3B="$LOG_DIR/phase3-bridge.log"

# 另起一个桥把状态帧发到 8892（不干扰界面占着的 8891），
# 用 python 收一帧，逐项核对键名是否一个不少、一个不多。
python3 - "$PROBE_PORT" <<'PY' >"$P3" 2>&1 &
import json
import socket
import sys

port = int(sys.argv[1])

# 与 shared/mdc_protocol.h 一一对应（t/seq/ts + 21 个数据字段）
expect = [
    "t", "seq", "ts",
    "man_sw", "man_bid", "man_v", "man_ic", "man_dc", "man_soc", "man_tmp",
    "auto_sw", "auto_bid", "auto_v", "auto_ic", "auto_dc", "auto_soc", "auto_tmp",
    "ccu_motor", "chg_mode", "chg_state", "fan", "hall",
    "man_link", "man_chg", "auto_link", "auto_chg",
]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", port))
sock.settimeout(8.0)

frame = None
try:
    while frame is None:
        raw, _ = sock.recvfrom(4096)
        obj = json.loads(raw.decode("utf-8"))
        if obj.get("t") == "s":
            frame = (obj, raw)
except socket.timeout:
    print("超时：8 秒内没收到状态帧")
    sys.exit(1)
except ValueError as exc:
    print("收到无法解析的报文：%s" % exc)
    sys.exit(1)

obj, raw = frame
print("收到状态帧，共 %d 个字段：" % len(obj))
print(raw.decode("utf-8"))

missing = [k for k in expect if k not in obj]
extra = [k for k in obj if k not in expect]

if missing:
    print("缺少字段：%s" % ", ".join(missing))
    sys.exit(1)
if extra:
    print("出现协议未定义的字段：%s" % ", ".join(extra))
    sys.exit(1)

print("全部 %d 个字段齐备，无多余字段" % len(expect))
sys.exit(0)
PY
PROBE=$!

sleep 0.5
"$BRIDGE" --ros-args -p peer_port:="$PROBE_PORT" >"$P3B" 2>&1 &
BRIDGE2=$!
PIDS+=($BRIDGE2)

wait $PROBE
rc=$?
kill "$BRIDGE2" 2>/dev/null || true

sed 's/^/        /' "$P3"

if [[ $rc -eq 0 ]]; then
  pass "协议字段与 shared/mdc_protocol.h 完全一致"
else
  fail "协议字段校验失败（python 退出码 $rc）"
  dump "$P3B"
fi

# ============================================================ 结果
step "结果"

if [[ $FAILED -eq 0 ]]; then
  printf '%s全部通过：编出来的二进制确实能跑，整条链路确实通。%s\n' "$GREEN" "$NC"
else
  printf '%s冒烟测试未通过，日志在 %s%s\n' "$RED" "$LOG_DIR" "$NC"
  printf '可以单独看：\n'
  printf '  %s\n' "$P1" "$P2F" "$P2B" "$P2A" "$P3" "$P3B"
fi

exit $FAILED
