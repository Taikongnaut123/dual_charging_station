#!/usr/bin/env bash
#
# 启动假设备 mdc_fake_pub —— 没有真机时用它造数据。
#
# 约 2Hz 发布 /mdc_data（与真机 500ms 一帧一致）。新协议只收不发，
# 所以它不再提供任何控制 Service。
#
# 用法：
#     ./run_fake.sh
#     ./run_fake.sh --ros-args -p log_level:=debug
#
set -euo pipefail

PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=_ros_env.sh
source "${PKG_ROOT}/_ros_env.sh"

exec "${PKG_ROOT}/bin/mdc_fake_pub" "$@"
