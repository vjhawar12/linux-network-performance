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
import plotly.graph_objects as go  # Add alongside your existing imports

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

    div[data-testid="stMetricLabel"] {
        color: #94A3B8 !important;
        text-transform: uppercase;
        font-size: 0.78rem;
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
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
        s.bind(unix_socket_path)
        s.settimeout(0.5)
        while time.monotonic() < deadline:
            try:
                data, sender_addr = s.recvfrom(data_size)
            except TimeoutError:
                continue
            json_data = json.loads(data.decode("utf-8"))
            if json_data["TX"] == True:
                queue_tx.append(json_data)
            else:
                queue_rx.append(json_data)

@st.fragment(run_every="0.1s")
def live_chart(queue_tx, queue_rx):
    series = (
        ("TX", "tx_bytes", list(queue_tx)),
        ("RX", "rx_bytes", list(queue_rx)),
    )
    first_timestamps = [
        samples[0]["timestamp_ms"] for _, _, samples in series if samples
    ]
    if not first_timestamps:
        return
    start_ms = min(first_timestamps)
    rows = []
    for direction, counter, samples in series:
        for previous, current in pairwise(samples):
            timestamp_ms = current["timestamp_ms"]
            interval_ms = timestamp_ms - previous["timestamp_ms"]
            byte_delta = current["data"][counter][1] - previous["data"][counter][1]
            if interval_ms <= 0 or byte_delta < 0:
                continue
            rows.append({
                "elapsed_s": (timestamp_ms - start_ms) / 1000,
                "gbps": 8 * byte_delta / (interval_ms * 1_000_000),
                "direction": direction,
            })
    if rows:
        df = pd.DataFrame(rows)
        rx_df = df[df["direction"] == "RX"]
        tx_df = df[df["direction"] == "TX"]

        # KPI Cards Row
        c1, c2, c3, c4 = st.columns(4)
        c1.metric(
            "RX Current",
            f"{rx_df['gbps'].iloc[-1]:.2f} Gbps" if not rx_df.empty else "0.00 Gbps",
        )
        c2.metric(
            "RX Peak",
            f"{rx_df['gbps'].max():.2f} Gbps" if not rx_df.empty else "0.00 Gbps",
        )
        c3.metric(
            "TX Current",
            f"{tx_df['gbps'].iloc[-1]:.2f} Gbps" if not tx_df.empty else "0.00 Gbps",
        )
        c4.metric(
            "TX Peak",
            f"{tx_df['gbps'].max():.2f} Gbps" if not tx_df.empty else "0.00 Gbps",
        )

        # Styled Plotly Chart
        fig = go.Figure()
        colors = {"RX": "#00D4FF", "TX": "#FF9F1C"}
        for direction in ["RX", "TX"]:
            sub = df[df["direction"] == direction].copy()
            if sub.empty:
                continue
            sub["smooth"] = sub["gbps"].rolling(window=5, min_periods=1).mean()
            fig.add_trace(
                go.Scatter(
                    x=sub["elapsed_s"],
                    y=sub["gbps"],
                    mode="lines",
                    line=dict(color=colors[direction], width=1),
                    opacity=0.25,
                    showlegend=False,
                )
            )
            fig.add_trace(
                go.Scatter(
                    x=sub["elapsed_s"],
                    y=sub["smooth"],
                    mode="lines",
                    name=f"{direction} (Gbps)",
                    line=dict(color=colors[direction], width=2.5),
                    fill="tozeroy",
                    fillcolor="rgba(0, 212, 255, 0.08)"
                    if direction == "RX"
                    else "rgba(255, 159, 28, 0.08)",
                )
            )

            fig.update_layout(
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
        #st.line_chart(rows, x="elapsed_s", y="gbps", color="direction")

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