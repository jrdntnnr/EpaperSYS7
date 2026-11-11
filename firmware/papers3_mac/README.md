# PaperS3 Macintosh firmware

Native ESP-IDF 5.5.4 build using the upstream esp32mac machine and esp32emu
Musashi/debug/diskspace components. No Arduino, RGB LCD, Wi-Fi or BLE runtime.

## SD setup

Use a FAT32 microSD card. Put extracted files at its root:

```text
roms/plus.rom       Macintosh Plus v3, 131072 bytes (ROM ID 4D1F8172)
disks/system.hfv   Bootable disk image with a System installed for the Plus
```

Optional `roms/se.rom` (256KB) and `roms/classic.rom` (512KB) are supported by
the core. The first recognized ROM in Plus, SE, Classic order is selected.
Optional `disks/work.hfv` and `disks/extra.hfv` mount as drives 2 and 3.
Guest RAM is 4MB. A System Folder from a "Minimal" install for another
model (e.g. the Performa 7.0.1P image) boots to a refusal dialog on the Plus;
check candidates first on the host build (`MAC_DISK1=image mac_host plus.rom`). Rename extracted raw `.dsk`/`.img` images to the prescribed
names; do not rename a `.zip`, `.sit`, `.bin` archive or MacBinary wrapper.
Disk Copy 6 NDIF images (`.img` with a `bcem` resource fork) must be
expanded first: `python3 tools/ndif_expand.py data.img data.img.rsrc system.hfv`.
Copy image files onto FAT32, not over the whole physical card.

On an empty card the firmware creates `roms` and `disks` directories and displays
instructions. Missing/unrecognized ROMs do not start the CPU. With a valid ROM
but no boot disk, the Macintosh displays its no-disk startup screen.

## Controls

- Layout: 14px margin around the 512x342 viewport with a 1px bezel. The
  panel below is drawn 1-bit at the guest's pixel scale with the System file's
  own Chicago 12 (`tools/mac_font.py` generates `main/font_chicago12.h`), System
  7 push buttons, radio buttons and checkboxes; greys are Bayer patterns.
- Trackpad: move in the dithered area; tap to click; hold still for half a
  second to hold the mouse button while dragging. Click holds until release.
  Drag lock keeps the button held; tap it again to release.
- Keyboard: Macintosh Plus layout plus Esc, Control and the cursor keys. Shift,
  Control, Option, Command and Caps are sticky and show inverted. Switching
  modes releases all keys/buttons.
- System: Pause/Continue, Restart, Clean Screen, Shut Down (flush, eject,
  unmount SD), Retry (remount and start). Display picks the panel drive: Fast
  (7 passes, no settle, grey blacks), Balanced (13 passes, 1.5 ms settle) or
  Sharp (13 passes, 4 ms settle). Saved in NVS; default Balanced. Shut down
  within Finder before Shut Down and wait for "Safe to remove the SD card".
  The status line at the bottom of the System body reports state changes.

The empty-card UI and touch can be tested without copyrighted media. Firmware
and release bundles never contain ROM or System software.

## Build and flash

From the workspace root, after `tools/bootstrap.sh` has fetched and patched the
sources. The panel font is generated from your own System file and is not in
the repository:

```sh
hmount system.hfv 1 && hcopy -m ':System Folder:System' System.bin && humount
python3 tools/mac_font.py System.bin Chicago 12 firmware/papers3_mac/main/font_chicago12.h
```

Then:

```sh
. .toolchains/esp-idf/export.sh
idf.py -C firmware/papers3_mac -DIDF_TARGET=esp32s3 build
idf.py -C firmware/papers3_mac -p /dev/cu.usbmodem1101 flash
```

If flashing leaves the chip in download mode, single-click the physical power
button, or use the installed IDF Python environment:

```sh
python tools/boot_capture.py /dev/cu.usbmodem1101 --seconds 30
```

Do not double-click power while guest disks are active. Flashing overwrites the
current board firmware and partition table; it does not format the SD card.

## Architecture and current limits

Core 1 owns the Macintosh machine and runs one frame at a time. An explicit
mutex gates pause/reset/storage operations. At frame boundaries it converts
changed video into a canonical 1bpp mailbox. Core 0 snapshots that mailbox,
diffs tiles and submits them to the asynchronous packed e-paper worker. The
UI and guest framebuffers do not overlap. Input uses the core's thread-safe
key/mouse API. Guest disks are flushed/synced every 60 emulated frames and before
safe stop. PRAM is persisted to NVS on pause/stop.

No Bluetooth, network, audio, battery shutdown, disk browser or automatic
periodic guest-viewport clean is provided in this build. The panel paints at the Display setting; Sharp measured about 112 ms per
paint, and the last paint time is logged every 5 s. The presenter runs once
per guest frame and the worker coalesces submits for 5 ms. Tabs clean automatically;
use Clean Screen for accumulated ghosting in the Macintosh viewport. The framebuffer and surface host tests pass; real System boot, input
inside Finder, sustained CPU rate, storage fault injection and ghosting endurance
still need a bootable user-supplied disk. Safe stop flushes host writes but cannot
force an uncooperative guest application to save its own data.
