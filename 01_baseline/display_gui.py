import streamlit as st
import pandas as pd
import numpy as np
import socket
import argparse
import os
import time
import json
import threading

parser = argparse.ArgumentParser()
parser.add_argument("socket_path")
parser.add_argument("packet_size")
parser.add_argument("duration_s")
args = parser.parse_known_args()
unix_socket_path = args["socket_path"]
data_size = args["packet_size"]
duration_seconds = args["duration_s"]

@st.cache_resource
def receive_data(socket_path, data_size, deadline, queue):
    try:
        os.unlink(socket_path)
    except OSError:
        if os.path.exists(socket_path):
            raise
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
        s.bind(unix_socket_path)
        s.settimeout(0.5)
        while time.monotonic() < deadline:
            data, sender_addr = s.recvfrom(data_size)
            json_data = json.loads(data.decode("utf-8"))
            queue.append(json_data)

@st.fragment(run_every="0.1s")
def live_chart(queue, deadline):
    if time.monotonic() >= deadline or not queue:
        return
    df = pd.DataFrame(queue, columns=["Value", "Timestamp_ms"])
    df["Time"] = pd.to_datetime(df["Timestamp_ms"], unit="ms")
    st.metric("Latest Value", df["Value"].iloc[-1])
    st.line_chart(df.set_index("Time")["Value"])

if __name__ == "__main__":
    deadline =  time.monotonic() + duration_seconds
    threads = []
    queue = []
    threads.append(threading.Thread(receive_data, args=(unix_socket_path, data_size, deadline, queue)))
    live_chart(queue, deadline)
    st.title("🔌 Unix Socket Live Telemetry")
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()