"""Capture a complete panel-probe cycle; requires ESP-IDF's pyserial environment."""
import argparse
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("--seconds", type=float, default=80)
parser.add_argument("--output", type=Path, default=Path("build/panel-cycle.log"))
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
# USB line transitions may reset the S3. Keep one connection through the entire
# 60-second pattern and final clean rather than reconnecting midway.
with serial.Serial(args.port, 115200, timeout=0.5) as port, args.output.open("wb") as log:
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        data = port.read(8192)
        if data:
            log.write(data)
            log.flush()
            print(data.decode(errors="replace"), end="", flush=True)
