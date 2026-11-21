import streamlit as st
import pandas as pd
import numpy as np
import socket
import argparse
import os
import time
import json
import threading
from itertools import pairwise
import plotly.graph_objects as go  

st.set_page_config(
    page_title="Unix Socket Live Telemetry", page_icon="🔌", layout="wide"
)

st.markdown(
    """
    <style>
    @import url('https://googleapis.com');

    /* Deep dark-slate radial gradient background */
    .stApp {
        background: radial-gradient(circle at 15% 15%, #111b2e 0%, #090d16 55%, #05070c 100%);
        color: #E2E8F0;
        font-family: 'Space Grotesk', sans-serif;
    }

    /* Hide default white top header bar so background flows edge-to-edge */
    header[data-testid="stHeader"] {
        background: transparent;
    }

    /* Crisp technical font for titles and headers */
    h1, h2, h3 {
        font-family: 'Space Grotesk', sans-serif !important;
        letter-spacing: -0.02em;
    }

    /* Monospace font for captions and metric numbers so digits don't jitter as they update */
    .stCaption, div[data-testid="stMetricValue"] {
        font-family: 'JetBrains Mono', monospace !important;
    }

    /* Glassmorphism KPI metric cards */
    div[data-testid="stMetric"] {
        background: rgba(17, 25, 40, 0.65);
        backdrop-filter: blur(12px);
        border: 1px solid rgba(0, 212, 255, 0.18);
        box-shadow: 0 4px 20px rgba(0, 0, 0, 0.35);
        padding: 14px 18px;
        border-radius: 12px;
    }

    div[data-testid="stMetricLabel"], div[data-testid="stMetricLabel"] p {
        color: #E2E8F0 !important;
        text-transform: uppercase;
        font-size: 0.85rem !important;
        font-weight: 600 !important;
        letter-spacing: 0.06em;
    }

    div[data-testid="stMetricValue"] {
        color: #F8FAFC !important;
        font-weight: 700;
    }
    </style>
""",
    unsafe_allow_html=True,
)

parser = argparse.ArgumentParser()
parser.add_argument("socket_path")
parser.add_argument("packet_size")
parser.add_argument("duration_s")
args, _ = parser.parse_known_args()
unix_socket_path = args.socket_path
data_size = int(args.packet_size)
duration_seconds = float(args.duration_s)

def receive_data(socket_path, data_size, deadline, queue_tx, queue_rx):
    try:
        os.unlink(socket_path)
    except OSError:
        if os.path.exists(socket_path):
            raise

    prev_tx, prev_rx = None, None
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
        s.bind(socket_path)
        s.settimeout(0.5)
        while time.monotonic() < deadline:
            try:
                data, sender_addr = s.recvfrom(data_size)
            except TimeoutError:
                continue

            json_data = json.loads(data.decode("utf-8"))
            is_tx = json_data.get("TX") is True
            prev = prev_tx if is_tx else prev_rx

            if prev is not None:
                dt = json_data["timestamp_ms"] - prev["timestamp_ms"]
                byte_key = "tx_bytes" if is_tx else "rx_bytes"
                drop_key = "tx_dropped" if is_tx else "rx_dropped"
                db = json_data["data"][byte_key][1] - prev["data"][byte_key][1]

                if dt > 0 and db >= 0:
                    cur_d = json_data["data"]
                    prv_d = prev["data"]

                    nic_drops = max(
                        0,
                        cur_d.get(drop_key, [0, 0])[1] - prv_d.get(drop_key, [0, 0])[1],
                    )
                    qdisc_drops = max(
                        0,
                        cur_d.get("qdisc_drops", [0, 0])[1]
                        - prv_d.get("qdisc_drops", cur_d.get("qdisc_drops", [0, 0]))[1],
                    )
                    rcv_errs = max(
                        0,
                        cur_d.get("RcvBufErrors", [0, 0])[1]
                        - prv_d.get("RcvBufErrors", cur_d.get("RcvBufErrors", [0, 0]))[1],
                    )

                    sample_tuple = (
                        json_data["timestamp_ms"],
                        8 * db / (dt * 1_000_000),
                        nic_drops,
                        qdisc_drops,
                        rcv_errs,
                    )
                    if is_tx:
                        queue_tx.append(sample_tuple)
                    else:
                        queue_rx.append(sample_tuple)

            if is_tx:
                prev_tx = json_data
            else:
                prev_rx = json_data

@st.fragment(run_every="0.5s")
def live_chart(queue_tx, queue_rx):
    tx_snap = list(queue_tx)
    rx_snap = list(queue_rx)

    first_timestamps = [snap[0][0] for snap in (tx_snap, rx_snap) if snap]
    if not first_timestamps:
        return

    start_ms = min(first_timestamps)
    rows = []
    for direction, snap in (("TX", tx_snap), ("RX", rx_snap)):
        for ts_ms, gbps, nic_d, qdisc_d, rcv_e in snap:
            rows.append({
                "elapsed_s": (ts_ms - start_ms) / 1000,
                "gbps": gbps,
                "nic_drops": nic_d,
                "qdisc_drops": qdisc_d,
                "RcvbufErrors": rcv_e,
                "direction": direction,
            })

    if rows:
        df = pd.DataFrame(rows)
        rx_df = df[df["direction"] == "RX"]
        tx_df = df[df["direction"] == "TX"]

        total_rx_drops = int(
            rx_df["nic_drops"].sum()
            + rx_df["qdisc_drops"].sum()
            + rx_df["RcvbufErrors"].sum()
        )
        total_tx_drops = int(
            tx_df["nic_drops"].sum()
            + tx_df["qdisc_drops"].sum()
            + tx_df["RcvbufErrors"].sum()
        )

        c1, c2, c3, c4, c5, c6 = st.columns(6)
        c1.metric(
            "RX Current",
            f"{rx_df['gbps'].iloc[-1]:.2f} Gbps" if not rx_df.empty else "0.00 Gbps",
        )
        c2.metric(
            "RX Peak",
            f"{rx_df['gbps'].max():.2f} Gbps" if not rx_df.empty else "0.00 Gbps",
        )
        c3.metric("RX Total Drops", f"{total_rx_drops:,}")
        c4.metric(
            "TX Current",
            f"{tx_df['gbps'].iloc[-1]:.2f} Gbps" if not tx_df.empty else "0.00 Gbps",
        )
        c5.metric(
            "TX Peak",
            f"{tx_df['gbps'].max():.2f} Gbps" if not tx_df.empty else "0.00 Gbps",
        )
        c6.metric("TX Total Drops", f"{total_tx_drops:,}")

        # First Chart: Throughput
        fig = go.Figure()
        colors = {"RX": "#00D4FF", "TX": "#FF9F1C"}
        for direction in ["RX", "TX"]:
            sub = df[df["direction"] == direction].copy()
            if sub.empty:
                continue
            sub["smooth"] = sub["gbps"].rolling(window=5, min_periods=1).mean()
            fig.add_trace(
                go.Scattergl(
                    x=sub["elapsed_s"],
                    y=sub["gbps"],
                    mode="lines",
                    name=f"{direction} Instantaneous",
                    line=dict(color=colors[direction], width=1),
                    opacity=0.25,
                    showlegend=False,
                )
            )
            fig.add_trace(
                go.Scattergl(
                    x=sub["elapsed_s"],
                    y=sub["smooth"],
                    mode="lines",
                    name=f"{direction} Rolling Avg",
                    line=dict(color=colors[direction], width=2.5),
                    fill="tozeroy",
                    fillcolor="rgba(0, 212, 255, 0.08)"
                    if direction == "RX"
                    else "rgba(255, 159, 28, 0.08)",
                )
            )

        fig.update_layout(
            uirevision="throughput_chart",
            template="plotly_dark",
            paper_bgcolor="rgba(17, 25, 40, 0.55)",
            plot_bgcolor="rgba(0, 0, 0, 0)",
            font=dict(family="JetBrains Mono, monospace", color="#CBD5E1"),
            height=420,
            margin=dict(l=15, r=15, t=35, b=15),
            xaxis_title="Elapsed (s)",
            yaxis_title="Gbps",
            yaxis=dict(rangemode="tozero", gridcolor="rgba(255,255,255,0.06)"),
            xaxis=dict(gridcolor="rgba(255,255,255,0.06)"),
            legend=dict(
                orientation="h",
                yanchor="bottom",
                y=1.02,
                xanchor="right",
                x=1,
                font=dict(color="#F8FAFC", size=13),
                bgcolor="rgba(17, 25, 40, 0.85)",
                bordercolor="rgba(255, 255, 255, 0.15)",
                borderwidth=1,
            ),
            hovermode="x unified",
        )
        st.plotly_chart(fig, width="stretch", theme=None)

        # Second Chart: Packet Drops Underneath (NIC, Qdisc & Socket RcvbufErrors)
        drop_fig = go.Figure()
        drop_styles = [
            ("RX", "RcvbufErrors", "RX UDP RcvbufErrors", "#C77DFF"),
            ("RX", "nic_drops", "RX NIC Drops", "#FF4B4B"),
            ("RX", "qdisc_drops", "RX Qdisc Drops", "#FF85A1"),
            ("TX", "nic_drops", "TX NIC Drops", "#FF9F1C"),
            ("TX", "qdisc_drops", "TX Qdisc Drops", "#008000"),
        ]
        for direction, col_name, label, color in drop_styles:
            sub = df[df["direction"] == direction]
            if sub.empty:
                continue
            drop_fig.add_trace(
                go.Scattergl(
                    x=sub["elapsed_s"],
                    y=sub[col_name],
                    mode="lines+markers",
                    name=label,
                    line=dict(color=color, width=2, shape="hv"),
                    marker=dict(size=4),
                )
            )

        max_drop = max(
            df["nic_drops"].max(),
            df["qdisc_drops"].max(),
            df["RcvbufErrors"].max(),
            0,
        )
        y_upper = max(5, max_drop * 1.2)

        drop_fig.update_layout(
            uirevision="drop_chart",
            title=dict(
                text="Packet Drops per Interval (Socket RcvBuf, NIC & Qdisc)",
                font=dict(color="#F8FAFC", size=15),
            ),
            template="plotly_dark",
            paper_bgcolor="rgba(17, 25, 40, 0.55)",
            plot_bgcolor="rgba(0, 0, 0, 0)",
            font=dict(family="JetBrains Mono, monospace", color="#CBD5E1"),
            height=300,
            margin=dict(l=15, r=15, t=45, b=15),
            xaxis_title="Elapsed (s)",
            yaxis_title="Dropped Packets",
            yaxis=dict(range=[0, y_upper], gridcolor="rgba(255,255,255,0.06)"),
            xaxis=dict(gridcolor="rgba(255,255,255,0.06)"),
            legend=dict(
                orientation="h",
                yanchor="bottom",
                y=1.02,
                xanchor="right",
                x=1,
                font=dict(color="#F8FAFC", size=12),
                bgcolor="rgba(17, 25, 40, 0.85)",
                bordercolor="rgba(255, 255, 255, 0.15)",
                borderwidth=1,
            ),
            hovermode="x unified",
        )
        st.plotly_chart(drop_fig, width="stretch", theme=None)


if __name__ == "__main__":
    deadline =  time.monotonic() + duration_seconds
    threads = []
    queue_tx = []
    queue_rx = []
    threads.append(threading.Thread(target=receive_data, args=(unix_socket_path, data_size, deadline, queue_tx, queue_rx), daemon=True))
    st.title("🔌 Unix Socket Live Telemetry")
    st.caption(
        f"Listening on `{unix_socket_path}` | Buffer: {data_size}B | Duration:" 
        f" {duration_seconds}s"
    )
    for thread in threads:
        thread.start()
    live_chart(queue_tx, queue_rx)