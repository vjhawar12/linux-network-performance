#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

cmake -S . -B build
cmake --build build -j

sudo -v

if [[ ! -e /sys/class/net/veth-tx && ! -e /sys/class/net/veth-rx ]]; then
    sudo ip link add veth-tx type veth peer name veth-rx
fi

if [[ ! -e /sys/class/net/veth-tx || ! -e /sys/class/net/veth-rx ]]; then
    echo "Expected both veth-tx and veth-rx in the current network namespace." >&2
    exit 1
fi

sudo ip link set dev veth-tx up
sudo ip link set dev veth-rx up

cd 02_af_packet 

.venv312/bin/python3 ./test_harness.py \
    --tx_intf_idx "$(cat /sys/class/net/veth-tx/ifindex)" \
    --rx_intf_idx "$(cat /sys/class/net/veth-rx/ifindex)" 