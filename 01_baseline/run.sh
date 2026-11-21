#!/bin/bash

TX_IF="veth-tx02"
RX_IF="veth-rx02"
RX_NS="rxbench-veth-02"

cleanup() {
    # Delete TX interface if it exists in the default namespace
    if ip link show dev "$TX_IF" >/dev/null 2>&1; then
        sudo ip link del dev "$TX_IF"
    fi

    # Delete RX interface if it exists in the default namespace
    if ip link show dev "$RX_IF" >/dev/null 2>&1; then
        sudo ip link del dev "$RX_IF"
    fi

    # Delete RX interface if it was moved into the target namespace
    if ip netns list | grep -qw "$RX_NS"; then
        if sudo ip netns exec "$RX_NS" ip link show dev "$RX_IF" >/dev/null 2>&1; then
            sudo ip netns exec "$RX_NS" ip link del dev "$RX_IF"
        fi
    fi
}

# Run cleanup before starting (ignoring errors) to clear leftover state
cleanup || true

set -e -o pipefail

# Register cleanup to run automatically when the script exits
trap cleanup EXIT

sudo ip link add "$TX_IF" type veth peer name "$RX_IF"

mkdir -p results
run_dir=$(mktemp -d results/veth-100m-XXXXXX)
printf 'Saving results to %s\n' "$run_dir"
printf 'Live watch: tail -n +1 -F %q %q\n' "$PWD/$run_dir/tx.txt" "$PWD/$run_dir/rx.txt"

(
  socket_path="$run_dir/telemetry.sock"

  .venv312/bin/python3 -m streamlit run display_gui.py \
    --server.headless true -- "$socket_path" 65536 30 &
  dashboard_pid=$!
  trap 'kill "$dashboard_pid" 2>/dev/null || true' EXIT

  printf 'Open http://localhost:8501 to start the dashboard receiver.\n'

  ready_by=$((SECONDS + 60))
  while [[ ! -S "$socket_path" ]]; do
    if (( SECONDS >= ready_by )) || ! kill -0 "$dashboard_pid" 2>/dev/null; then
      printf 'Dashboard did not bind %s\n' "$socket_path" >&2
      exit 1
    fi
    sleep 0.1
  done

  sudo .venv312/bin/python3 test_harness.py "$TX_IF" "$RX_IF" \
    --rx-namespace "$RX_NS" \
    --tx-ip 172.31.251.1 --rx-ip 172.31.251.2 \
    --bitrate 10G --duration 10 \
    --tx_file "$run_dir/tx.txt" \
    --rx_file "$run_dir/rx.txt" \
    "$socket_path" \
    2>&1 | tee "$run_dir/summary.txt"

  printf 'Benchmark finished. Press Ctrl+C when done viewing the chart.\n'
  wait "$dashboard_pid"
)