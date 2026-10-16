#!/usr/bin/env bash
#
# 启动假设备 mdc_fake_pub —— 没有真机时用它造数据。
#
# 以 10Hz 发布 /mdc_data，并代管 5 个控制 Service，所以「界面 → 桥 → 服务」
# 整条链路都能在没有硬件的情况下验证。
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
