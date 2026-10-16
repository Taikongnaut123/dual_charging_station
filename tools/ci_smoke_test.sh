#!/usr/bin/env bash
#
# ============================================================================
#  ci_smoke_test.sh —— 冒烟测试：不只验证"编过了"，而是验证"真的跑得起来"
# ============================================================================
#
#  三个阶段，任一失败 → 退出码 1：
#
#    阶段 1  无头启动界面（环回模式）
#            证明：二进制能启动、5 个页面都能构造、事件循环在跑、定时器在跳
#
#    阶段 2  假设备 → 桥 → UDP → 界面  全链路
#            证明：界面确实收到了 10Hz 真实数据流，而不是"只是没崩"
#
#    阶段 3  模拟界面下发 5 条控制指令
#            证明：UDP → 桥 → ROS2 服务 这一向也通，且假设备收到了正确参数
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
#     ROS_DISTRO   ROS2 发行版，默认 humble
#     LOG_DIR      日志目录，默认 build/ci-logs
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS="$ROOT/ros2_ws"
APP="$ROOT/build/dual_charging_station"
BRIDGE="$WS/install/mdc_bridge/lib/mdc_bridge/mdc_bridge"
FAKE="$WS/install/mdc_bridge/lib/mdc_bridge/mdc_fake_pub"
ROS_DISTRO="${ROS_DISTRO:-humble}"
LOG_DIR="${LOG_DIR:-$ROOT/build/ci-logs}"

# 桥监听控制指令 / 界面监听状态帧，与 shared/mdc_protocol.h 里的默认值一致
CMD_PORT=8890
STAT_PORT=8891

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
# shellcheck disable=SC1090
source "$WS/install/setup.bash"
set -u

"$FAKE"   >"$P2F" 2>&1 & PIDS+=($!)
sleep 1
"$BRIDGE" >"$P2B" 2>&1 & PIDS+=($!)
sleep 2
"$APP" --verbose >"$P2A" 2>&1 & PIDS+=($!)

info "三个进程已启动，等 12 秒看链路是否打通 ..."
sleep 12

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

  if [[ "${FRAMES:-0}" -ge 30 ]]; then
    pass "界面 12 秒内收到 ${FRAMES} 帧（假设备按 10Hz 上报，阈值 ≥30 通过）"
  else
    fail "界面只收到 ${FRAMES:-0} 帧，链路没打通"
    dump "$P2A"
    dump "$P2B"
  fi

  if [[ "${RATE:-0}" -ge 5 ]]; then
    pass "帧率 ${RATE}Hz（阈值 ≥5Hz 通过）"
  else
    fail "帧率只有 ${RATE:-0}Hz，数据不连续"
  fi

  if grep -q '链路=在线' <<<"$LAST_STAT"; then
    pass "界面链路三态判定 = 在线"
  else
    fail "界面链路状态不是「在线」：$LAST_STAT"
  fi
fi

# ============================================================ 阶段 3
step "阶段 3／3　模拟界面下发 5 条控制指令（UDP → 桥 → ROS2 服务）"

P3="$LOG_DIR/phase3-send.log"

python3 - "$CMD_PORT" <<'PY' >"$P3" 2>&1
import json
import socket
import sys
import time

port = int(sys.argv[1])

# 报文格式见 shared/mdc_protocol.h：
#   下行  {"t":"c","id":1,"cmd":"relay","relay1":1,"relay2":0}
#   应答  {"t":"ack","id":1,"cmd":"relay","ok":true,"msg":"..."}
cmds = [
    (1, "relay",   {"relay1": 1, "relay2": 0}),
    (2, "fan",     {"fan1": 128, "fan2": 64, "fan3": 0, "fan4": 0}),
    (3, "powerio", {"ctrl": 15}),
    (4, "wch",     {"payload": [1, 54, 1, 1]}),
    (5, "led",     {"ch1_mode": 1, "ch1_duty": 100,
                    "ch2_mode": 1, "ch2_duty": 50,
                    "ch3_mode": 1, "ch3_duty": 25,
                    "rgb_mode": 1, "r_duty": 255, "g_duty": 0, "b_duty": 128}),
]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5.0)

pending = {}
for cid, name, args in cmds:
    pkt = {"t": "c", "id": cid, "cmd": name}
    pkt.update(args)
    sock.sendto(json.dumps(pkt).encode("utf-8"), ("127.0.0.1", port))
    pending[cid] = name
    print("发送 #%d %s %s" % (cid, name, json.dumps(args, ensure_ascii=False)))
    time.sleep(0.3)

ok_count = 0
deadline = time.time() + 15
while pending and time.time() < deadline:
    try:
        data, _ = sock.recvfrom(4096)
    except socket.timeout:
        break
    try:
        msg = json.loads(data.decode("utf-8"))
    except ValueError:
        continue
    if msg.get("t") != "ack":
        continue
    cid = msg.get("id")
    if cid not in pending:
        continue
    name = pending.pop(cid)
    ok = bool(msg.get("ok"))
    ok_count += 1 if ok else 0
    print("应答 #%d %s -> %s (%s)" % (cid, name, "成功" if ok else "失败", msg.get("msg")))

for cid, name in sorted(pending.items()):
    print("应答 #%d %s -> 超时未收到" % (cid, name))

print("成功 %d/%d" % (ok_count, len(cmds)))
sys.exit(0 if ok_count == len(cmds) else 1)
PY
rc=$?

sed 's/^/        /' "$P3"

if [[ $rc -eq 0 ]]; then
  pass "5 条控制指令全部收到成功应答"
else
  fail "有控制指令失败或超时（python 退出码 $rc）"
fi

sleep 1

# 桥与假设备两侧交叉验证：不只看界面收到了应答，还要看对端确实处理了
BRIDGE_HITS="$(grep -c '控制指令 #[0-9]* [a-z]* -> 成功' "$P2B" || true)"
FAKE_HITS="$(grep -c '\[假服务\]' "$P2F" || true)"
info "桥日志中「-> 成功」${BRIDGE_HITS} 次；假设备日志中「[假服务]」${FAKE_HITS} 次"

if [[ "${BRIDGE_HITS:-0}" -ge 5 ]]; then
  pass "桥侧确认 5 次服务调用成功"
else
  fail "桥侧成功记录不足（${BRIDGE_HITS:-0} < 5）"
  dump "$P2B"
fi

if [[ "${FAKE_HITS:-0}" -ge 5 ]]; then
  pass "假设备侧确认收到了 5 次指令"
else
  fail "假设备侧没有收全指令（${FAKE_HITS:-0} < 5）"
  dump "$P2F"
fi

# ============================================================ 结果
step "结果"

if [[ $FAILED -eq 0 ]]; then
  printf '%s全部通过：编出来的二进制确实能跑，整条链路确实通。%s\n' "$GREEN" "$NC"
else
  printf '%s冒烟测试未通过，日志在 %s%s\n' "$RED" "$LOG_DIR" "$NC"
  printf '可以单独看：\n'
  printf '  %s\n' "$P1" "$P2F" "$P2B" "$P2A" "$P3"
fi

exit $FAILED
