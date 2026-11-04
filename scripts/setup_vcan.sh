#!/usr/bin/env bash
# Creates (or removes) a virtual CAN interface for development without hardware.
#   sudo scripts/setup_vcan.sh [IFACE]          create and bring up IFACE (default vcan0)
#   sudo scripts/setup_vcan.sh --down [IFACE]   remove IFACE
# The MTU is set to 72 so CAN FD frames (up to 64 bytes) can be sent.
set -euo pipefail

action=up
if [[ "${1:-}" == "--down" ]]; then
  action=down
  shift
fi
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '2,5p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
fi
iface="${1:-vcan0}"

if [[ $EUID -ne 0 ]]; then
  echo "error: run as root (sudo $0 ...)" >&2
  exit 1
fi
if ! command -v ip >/dev/null 2>&1; then
  echo "error: 'ip' (iproute2) is required" >&2
  exit 1
fi

if [[ "$action" == "down" ]]; then
  ip link delete "$iface" 2>/dev/null || true
  echo "$iface removed"
  exit 0
fi

modprobe vcan
if ! ip link show "$iface" >/dev/null 2>&1; then
  ip link add dev "$iface" type vcan
fi
ip link set "$iface" mtu 72
ip link set up "$iface"
echo "$iface is up (mtu 72, CAN FD enabled)"
