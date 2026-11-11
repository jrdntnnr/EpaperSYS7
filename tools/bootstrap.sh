#!/bin/sh
# Fetch the pinned sources and apply the local patches. Safe to re-run.
set -e
cd "$(dirname "$0")/.."
git submodule update --init --recursive esp32mac esp32emu vendor/EPD_Painter
git -C esp32mac apply --check ../patches/esp32mac-storage.patch 2>/dev/null \
    && git -C esp32mac apply ../patches/esp32mac-storage.patch
git -C vendor/EPD_Painter apply --check ../../patches/epd-painter-idf.patch 2>/dev/null \
    && git -C vendor/EPD_Painter apply ../../patches/epd-painter-idf.patch
echo "sources ready; ESP-IDF v5.5.4 is expected at .toolchains/esp-idf"
