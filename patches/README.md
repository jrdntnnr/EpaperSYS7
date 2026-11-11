# EPD_Painter native-IDF patch

Source: https://github.com/tonywestonuk/EPD_Painter
Pinned commit: `6335a149c5a9b3be4150880e0be64ef2529ba62a`.
License: Apache-2.0; original LICENSE and source notices remain in the checkout.

`epd-painter-idf.patch` records local changes against that revision. Apply to a
fresh checkout with `git -C vendor/EPD_Painter apply ../../patches/epd-painter-idf.patch`.
The current workspace checkout already has these changes; do not apply twice.

Changes replace remaining Arduino calls with platform HAL macros, use native
peripheral/GDMA APIs, preserve IRAM attributes, and check DMA/buffer/semaphore/task
creation before allowing startup. The boot shutdown helper is marked noreturn. Automatic reset-toggle
shutdown is disabled by our adapter. NVS waveform override loading is compiled
out for a reproducible initial probe. A new completion barrier waits for the active semaphore: upstream's paintIdle()
can become true before the last physical drive pass finishes. This barrier keeps
our wait/clean acknowledgement from racing the panel. The upstream waveform
tables are unchanged.

The current upstream allocates four 129,600-byte packed planes (one preferentially
internal, three in PSRAM), plus DMA and decision-engine structures. Our adapter
adds two 129,600-byte PSRAM buffers. No 518,400-byte 8bpp canvas is allocated.
The report's original memory estimate is therefore incomplete for this revision;
measure actual heap before enabling BLE, Wi-Fi or 4MB guest RAM.

## Macintosh storage barrier

`esp32mac-storage.patch` adds `mac_disk_flush()` (fflush + fsync), called between
frames and on safe stop by the PaperS3 runtime. It also propagates a failed
write-buffer flush to the guest instead of silently reporting write success.
Baseline is recorded in `sources.lock.json`; the host build still passes.
