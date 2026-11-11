"""Leave ESP32-S3 download mode with a watchdog reset and capture startup."""
import argparse
import time
from pathlib import Path
import serial
from esptool.targets.esp32s3 import ESP32S3ROM
p=argparse.ArgumentParser()
p.add_argument("port")
p.add_argument("--output",type=Path,default=Path("build/mac-startup.log"))
p.add_argument("--seconds",type=float,default=30)
a=p.parse_args()
s=serial.Serial(a.port,115200,timeout=.5)
esp=ESP32S3ROM(s)
esp.connect(mode="no_reset",attempts=2)
esp.write_reg(esp.RTC_CNTL_OPTION1_REG,0,esp.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
esp.watchdog_reset()
a.output.parent.mkdir(parents=True,exist_ok=True)
end=time.monotonic()+a.seconds
with a.output.open("wb") as log:
    while time.monotonic()<end:
        try:
            data=s.read(8192)
            if data:
                log.write(data);log.flush()
                print(data.decode(errors="replace"),end="",flush=True)
        except serial.SerialException:
            s.close();time.sleep(.5)
            try:s=serial.Serial(a.port,115200,timeout=.5)
            except serial.SerialException:continue
s.close()
