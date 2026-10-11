# T-RGB round display — plan

## Resume here
- **Board:** LilyGO T-RGB **2.8" full circle** (touch expected: GT911 — not yet confirmed by I²C scan).
- **Done:** detected (303a:1001, /dev/ttyACM0), serial perms OK, PlatformIO 6.1.19,
  researched board (memory: board-lilygo-t-rgb), **factory flash backed up + verified** (`backup/`).
- **Scaffolded** (approach A): `platformio.ini` (espressif32@6.9.0 = Arduino core 2.0.17,
  vendor board JSON in `boards/`), `include/board_pins.h`, `scripts/read_serial.py`.
- **`src/main.cpp`:** Stage 1 hello-serial (chip/flash/PSRAM info, counter, BOOT button).
  **Stage 1 DONE** (flashed, all predictions held; lesson-01, snapshot `stages/01-hello-serial/`).
  Factory demo is gone from flash (restore from `backup/`). Screen black.
- **Next:** Stage 2 — first light via vendor lib `LilyGo_RGBPanel` (lib_deps pinned to T-RGB @ 359c03f,
  SensorLib 0.2.3, GFX Library for Arduino 1.5.0); I²C scan to confirm GT911; colour bars + text.

## Board facts (measured)
- ESP32-S3 (QFN56) rev v0.1, 8 MB embedded octal PSRAM, 16 MB Winbond flash (quad), MAC 34:85:18:a9:7a:58.

## Decisions
- **Approach A:** build on LilyGO's library at their pinned versions (core 2.x, LVGL 8);
  learn by reading `LilyGo_RGBPanel.cpp`. Reason: panel init goes through the XL9555 expander with
  per-variant command tables — a wrong guess is a silent black screen. Revisit B (Arduino_GFX/core 3.x) later.
- Stage 1 has no lib_deps, so any failure is the toolchain, not a library.
- Factory backup via chunked reader with ROM fallback (G42) — one-shot read fails deterministically.

## Open questions
- Confirm touch chip = GT911 (I²C scan: expect 0x5D or 0x14, plus XL9555 at 0x20).
- Does the vendor library work on Arduino core 3.x, or stay on 2.x (espressif32@6.9.0)?
- GPIO46 (backlight) is a strapping pin: GPIO46=1 with GPIO0=0 at reset is invalid — keep BL low at reset.
