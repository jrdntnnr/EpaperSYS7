# EpaperSYS7

A Macintosh Plus running System 7 on the M5Stack PaperS3 e-paper tablet.
The Mac's 512x342 screen is shown 1:1 at the top of the 540x960 panel; below
it is a trackpad, a Plus keyboard and a System panel drawn in 1-bit with the
real Chicago 12 font.

The emulator core is [esp32mac](https://github.com/rlucencic/esp32mac)
(Musashi 68000); the panel driver is
[EPD_Painter](https://github.com/tonywestonuk/EPD_Painter). This repository
adds the native ESP-IDF firmware, the e-paper presenter and the controls.

## What you need

- M5Stack PaperS3 (ESP32-S3, 540x960 e-paper, touch, microSD).
- A microSD card formatted FAT32.
- A Macintosh Plus ROM dump (v3, 128 KB, checksum 4D1F8172). Dump it from your
  own machine; ROMs are not distributed here.
- A System 7.0.1 hard disk image that was installed for the Plus (see below).

## Get the firmware

Download the latest `EpaperSYS7-<version>.bin` from the
[Releases page](https://github.com/jrdntnnr/EpaperSYS7/releases). It is a
merged image (bootloader, partition table and app) for flashing at offset 0.

Connect the PaperS3 over USB-C and flash with
[esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/installation.html):

```sh
pip install esptool
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 write_flash 0x0 EpaperSYS7-v0.1.0.bin
```

Replace the port with yours (`COM5` on Windows, `/dev/ttyACM0` on Linux). If
esptool cannot connect, press the reset button and run it again; if the panel
stays blank after flashing, press reset once more.

Or build from source: see [Building](#building).

## Prepare the SD card

Create these paths on the FAT32 card. Names are exact.

```text
roms/plus.rom       Macintosh Plus v3 ROM, 131072 bytes     required
disks/system.hfv    startup disk, raw HFS or Disk Copy 4.2  required
disks/work.hfv      second drive                            optional
disks/extra.hfv     third drive                             optional
roms/se.rom         Macintosh SE ROM, 256 KB                optional
roms/classic.rom    Macintosh Classic ROM, 512 KB           optional
```

The first ROM found in Plus, SE, Classic order is used. On an empty card the
firmware creates `roms` and `disks` and shows these instructions on screen.

### Where to get the disk images

Apple released System 7.0.1 and earlier as free downloads. Two ready-made
Macintosh Plus hard disk images on the Internet Archive boot straight to the
Finder on this firmware:

- [mac_MacOS_7.0.1_compilation](https://archive.org/details/mac_MacOS_7.0.1_compilation):
  copy `hd1.img` to `disks/system.hfv` and `hd2.img` to `disks/work.hfv`.
- [AppleMacintoshSystem701](https://archive.org/details/AppleMacintoshSystem701):
  copy `System7_0_1.img` to `disks/system.hfv`.

Images from other sources must have been installed for the Plus. A System
Folder from a "Minimal" install for another model (for example a Performa
7.0.1P image) boots to a refusal dialog. Accepted formats are raw HFS, Disk
Copy 4.2 and Apple partition maps (first HFS partition). Disk Copy 6 NDIF
images must be expanded first:

```sh
python3 tools/ndif_expand.py data.img data.img.rsrc system.hfv
```

Copy files onto the card as files; do not write an image over the whole card.

## Using it

- Insert the card, power on. The Mac boots in a few seconds and runs at about
  60 frames per second; the panel repaints as fast as the e-paper allows.
- Tap the row under the Mac screen to switch between Trackpad, Keyboard and
  System.
- Trackpad: slide to move, tap to click, hold still for half a second to drag.
  Click holds the button while pressed; Drag lock keeps it held.
- Keyboard: Macintosh Plus layout plus Esc, Control and arrows. Shift,
  Control, Option, Command and Caps stay down until tapped again.
- System: Pause/Continue, Restart, Clean Screen, Shut Down, Retry, and the
  Display setting (Fast, Balanced, Sharp) that trades e-paper contrast for
  update speed. Shut down inside the Finder first, then choose Shut Down and
  wait for "Safe to remove the SD card" before pulling the card.

Details are in [firmware/papers3_mac/README.md](firmware/papers3_mac/README.md).

## Building

Requires ESP-IDF v5.5.4 at `.toolchains/esp-idf` and Python 3.

```sh
git clone https://github.com/jrdntnnr/EpaperSYS7.git
cd EpaperSYS7
tools/bootstrap.sh                      # submodules + local patches
. .toolchains/esp-idf/export.sh
idf.py -C firmware/papers3_mac -DIDF_TARGET=esp32s3 build
idf.py -C firmware/papers3_mac -p /dev/cu.usbmodem1101 flash
```

The panel font is generated from your own System file and is not in the
repository. With [hfsutils](https://www.mars.org/home/rob/proj/hfs/) installed:

```sh
hmount system.hfv 1 && hcopy -m ':System Folder:System' System.bin && humount
python3 tools/mac_font.py System.bin Chicago 12 firmware/papers3_mac/main/font_chicago12.h
```

Host tests and the host emulator build:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
make -C esp32mac/components/macintosh/host
MAC_DISK1=system.hfv esp32mac/components/macintosh/host/build/mac_host plus.rom
```

## Layout

- `firmware/papers3_mac` the PaperS3 firmware (main loop, UI, storage).
- `components/paper_presenter` dirty-tile presenter from Mac frame to panel.
- `components/paper_epd` paint worker and display speed presets.
- `patches/` local changes to the pinned submodules.
- `tools/` font extraction, NDIF expansion, ROM check, boot capture.
- [THIRD_PARTY.md](THIRD_PARTY.md) licenses of the bundled sources.

ROMs, disk images and System software are never committed or bundled.
