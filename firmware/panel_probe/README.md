# PaperS3 panel probe

Native ESP-IDF 5.5.4; no Arduino component, ROM or disk required.

From the workspace root after installing ESP-IDF:

```sh
. .toolchains/esp-idf/export.sh
idf.py -C firmware/panel_probe -DIDF_TARGET=esp32s3 build
# Once a PaperS3 is connected (this overwrites its current firmware):
idf.py -C firmware/panel_probe -p /dev/cu.usbmodemYOUR_DEVICE flash monitor
```

The probe clears the panel, draws a 512×342 border with a diagonal and a filled
top-left marker, holds for sixty seconds, then clears again. In portrait, the
viewport must start at x=14,y=0, with 14-pixel side margins and blank space below
y=341. Logs report internal free heap, largest block, and free PSRAM.

Hardware acceptance: confirm rotation, polarity, all four border edges, clean
recovery, no reset/watchdog and adequate heap. Record board revision, serial log
and a photograph. No automatic system shutdown, SD access or emulator boot occurs.
This is a bring-up probe; the user has confirmed the corrected orientation; sustained visual quality and
electrical behavior remain unqualified.

## Backend contract

One producer task owns the public API. Submit copies a packed portrait rectangle
into a physical packed shadow; false means invalid input or mutex contention and
nothing was accepted. Flush schedules the latest shadow, coalescing intermediate
states without losing unaffected pixels. A second packed buffer isolates the
worker's snapshot from later writes. The worker runs on core 0 and calls the
upstream packed painter, rate-limited to at most 20 requests/second.

Wait-idle flushes and waits for a worker acknowledgement. Full-clean first drains
accepted work and then schedules a clear on that same worker. Its timeout applies
to each of these two waits. On timeout, stop producing: the operation may still
be running; reboot/recover rather than treating the panel as synchronized.
After a successful clean, invalidate the presenter before submitting again.

Initialization is process-lifetime and may block for panel operations. Failed
initialization requires reboot. Full system power-off, disk barriers, touch and
idle-quality reconciliation are not implemented.

If serial reports `waiting for download` after flashing, release BOOT and press
RESET once. USB hard reset can retain download mode. The probe logs once per
second while the pattern is visible and every five seconds after the final clean.
Portrait packing follows EPD_Painter ROTATION_CW: physical `(y, 539-x)`.

Capture an entire cycle with the IDF Python environment active:

```sh
python tools/capture_probe.py /dev/cu.usbmodem1101 --seconds 80
```

Run this command from the workspace root. Opening USB serial may reset the chip;
keep the same connection through the complete cycle. Logs go to
`build/panel-cycle.log` by default.
