# Third-party source notices

- esp32mac: rlucencic, MIT. See `esp32mac/LICENSE`. Machine implementation used;
  its LGPL web archive converter is not linked into the PaperS3 firmware.
- esp32emu: rlucencic, MIT. See `esp32emu/LICENSE`; Musashi, diskspace and emudbg
  components are used. Their original source headers and notices remain intact.
- Musashi: Karl Stenerud and embedded-fork contributors; MIT notices remain in
  `esp32emu/components/musashi/musashi/`.
- EPD_Painter: Tony Weston and contributors, Apache-2.0. See
  `vendor/EPD_Painter/LICENSE`. Native-IDF changes recorded in `patches/`.
- ESP-IDF 5.5.4: Espressif and component authors; Apache-2.0 and per-component
  licenses in `.toolchains/esp-idf`. Includes FreeRTOS (MIT), FatFs and other
  components retaining their upstream notices.
- 8x8 font: Daniel Hepper, based on Marcel Sondaar / IBM VGA public-domain fonts.
  Original public-domain notice retained in `firmware/papers3_mac/main/font8x8.h`.

Apple ROMs and System software are supplied separately by the user and are not
included in firmware, patches or release bundles.
