#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sudo timeout 15s "$ROOT_DIR/build/linux_network_performance" \
    veth-tx veth-rx \
    "$(cat /sys/class/net/veth-tx/address)" \
    "$(cat /sys/class/net/veth-rx/address)" \
<<<<<<< HEAD
    Hello 5000000000 10 1
=======
    Hello 100000000000 10 1
>>>>>>> ee452300619afdebdbc13499627c5670b02590ec
