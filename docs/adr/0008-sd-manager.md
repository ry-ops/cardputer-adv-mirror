# ADR 0008 — SD card manager: explicit formatting via the FATFS layer

**Status:** Superseded by 0036 — SD support removed from the tree entirely; this record stays for the FATFS reasoning, which is still correct, just no longer in use
**Context:** checklist item "sd card formatter"

## Problem

Arduino's `SD.begin(cs, spi, freq, mount, max_files, format_if_empty)` can
format, but only as a side effect of a *failed mount*. It cannot:

- reformat a card that currently mounts fine (the common case — you want the
  card wiped, not repaired),
- choose FAT32 vs FAT16,
- set a cluster size,
- report progress or a specific failure.

A "formatter" built on that flag would only ever fix an already-broken card.

## Hardware verification first

Pins were read from `M5Unified`'s board table rather than assumed, because the
ADV differs from the original Cardputer elsewhere (audio codec, IMU, battery):

    _pin_table_sd[]: { board_M5CardputerADV, CLK 40, MOSI 14, MISO 39, -, -, CS 12 }

Identical to `board_M5Cardputer`. Two earlier greps looked like they had found
ADV SD pins (`7/6/8` and `19/33`) — both were **ATOM** boards inside
`external_speaker` branches. Worth recording: `grep` for a pin name in
M5Unified lands in unrelated board blocks constantly.

Bus contention was checked, not assumed. M5GFX configures the ADV display on
`SPI3_HOST` (MOSI 35 / SCLK 36 / DC 34 / RST 33). The SD manager therefore
takes `SPI2_HOST`. Different host, different pins, no arbitration needed.

## Decision

Drive the ESP-IDF FATFS layer directly.

`esp_vfs_fat_sdcard_format()` — the one-call formatter — **does not exist in
this toolchain**. The bundled IDF is 4.4 (`ESP_IDF_VERSION_MAJOR 4`,
`MINOR 4`); that API arrived in 5.x. Verified by grepping `esp_vfs_fat.h`:
zero matches.

What *is* available, and is what IDF 5.x's implementation does internally:

    pdrv = ff_diskio_get_pdrv_card(card)   // find the card's FATFS volume
    esp_vfs_fat_unregister_path("/sd")     // detach from VFS
    f_mount(NULL, drv, 0)                  // unmount, keep diskio registered
    f_mkfs(drv, FM_FAT32, au, work, 4096)  // the actual format
    esp_vfs_fat_register(...) + f_mount    // reattach

Leaving the diskio registration alive across the format is the load-bearing
detail: it means `f_mkfs` still has a working sector-level path to the card
without re-probing it over SPI.

`FF_USE_MKFS` is `1` in this build's `ffconf.h`, so `f_mkfs` is compiled in.
`FF_USE_LABEL` is `0`, so volume labels are not offered — the UI would have
had a text field that silently did nothing.

## Threading — the constraint that shaped the API

`f_mkfs()` blocks for seconds and wants a real stack. AsyncWebServer handlers
run on the `async_tcp` task, which has neither the stack headroom nor the
watchdog tolerance for that. Calling format from a request handler would panic
the device mid-wipe.

So the HTTP layer only ever *queues*:

    POST /api/sd/format  ->  sets s_formatPending, returns 202 immediately
    sdmgr::update()      ->  called from loop(), does the real work

The browser polls `/api/sd/status` at 700 ms while state is `formatting`, and
5 s otherwise. `202 Accepted` is the correct code here and says exactly this:
accepted, not finished.

The 4 KB `f_mkfs` work buffer is heap-allocated, not a stack local —
`loop()`'s stack cannot absorb it either.

## Safety

Destructive, one click from harmless buttons, so:

- `confirm=FORMAT` is required as a query parameter; a bare POST is refused
  with 400. Browsers issue stray POSTs far too easily to allow otherwise.
- The browser additionally requires a `confirm()` *and* typing `FORMAT` into a
  prompt.
- `format_if_mount_failed = false` on the normal mount path, so nothing is
  ever formatted implicitly.

## Card-size edge case

A card too small to reach FAT32's 65,525-cluster minimum makes `f_mkfs` return
`FR_MKFS_ABORTED`. Rather than surfacing that as a failure, the code retries
once with `FM_ANY` and lets FatFs pick FAT16/FAT12. Old 32 MB cards format
correctly instead of reporting a confusing error.

## Consequences

- Real reformat of a healthy card, with a chosen filesystem type.
- `+1.5 KB` flash; no measurable RAM change at idle.
- An unformatted card is now reported distinctly (`ESP_FAIL` from mount ->
  state `unformatted`) rather than being conflated with "no card".
- Tied to IDF 4.4 internals. If the Arduino core moves to IDF 5.x, `f_mkfs`
  still works, but `esp_vfs_fat_sdcard_format()` becomes available and this
  should collapse to a single call.

## Also in this change: mDNS

The hotspot's DHCP lease is dynamic (the device came up at `172.20.10.5`, and
that is not guaranteed across reboots). `wifimgr::begin()` now takes a
`hostname`, defaulting to `cardputer`:

- `WiFi.setHostname()` **before** `WiFi.begin()` — the name travels in the
  DHCP DISCOVER, so setting it after association is silently ineffective and
  the router keeps showing `espressif`.
- `MDNS.begin(hostname)` + `addService("http","tcp",80)` after a successful
  join, and after SoftAP too, so `http://cardputer.local` is the one address
  that is correct in both outcomes.
