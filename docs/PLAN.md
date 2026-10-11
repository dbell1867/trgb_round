# T-RGB round display — plan

## Resume here
- **Board:** LilyGO T-RGB **2.8" full circle**, touch GT911 @ 0x5D (confirmed).
- **Done:** detected (303a:1001, /dev/ttyACM0), serial perms OK, PlatformIO 6.1.19,
  researched board (memory: board-lilygo-t-rgb), **factory flash backed up + verified** (`backup/`).
- **Scaffolded** (approach A): `platformio.ini` (espressif32@6.9.0 = Arduino core 2.0.17,
  vendor board JSON in `boards/`), `include/board_pins.h`, `scripts/read_serial.py`.
- **Stage 1 DONE** (hello serial; lesson-01, snapshot `stages/01-hello-serial/`).
  Factory demo is gone from flash (restore from `backup/`).
- **`src/main.cpp`:** Stage 2b text demo (flashed, on screen).
- **Stage 2a DONE — first light** (vendor lib + pushColors colour bars; colours + orientation correct;
  GT911 confirmed at 0x5D). lesson-02, snapshot `stages/02a-first-light/`.
- **Stage 2b DONE — text** (Arduino_GFX canvas in PSRAM → pushColors; strip updates 1.6 ms vs 24 ms full;
  no panel offset, r=239 ring fully visible). lesson-02, snapshot `stages/02b-text/`.
- **Next:** Stage 3 — touch (GT911 via `panel.getPoint()`), e.g. draw where you touch.

## Board facts (measured)
- ESP32-S3 (QFN56) rev v0.1, 8 MB embedded octal PSRAM, 16 MB Winbond flash (quad), MAC 34:85:18:a9:7a:58.

## Decisions
- **Approach A:** build on LilyGO's library at their pinned versions (core 2.x, LVGL 8);
  learn by reading `LilyGo_RGBPanel.cpp`. Reason: panel init goes through the XL9555 expander with
  per-variant command tables — a wrong guess is a silent black screen. Revisit B (Arduino_GFX/core 3.x) later.
- Stage 1 has no lib_deps, so any failure is the toolchain, not a library.
- Factory backup via chunked reader with ROM fallback (G42) — one-shot read fails deterministically.

- Drawing: Arduino_GFX `Arduino_Canvas(480,480,nullptr)` off-screen + vendor `pushColors`; push dirty strips only.

## Open questions
- Does the vendor library work on Arduino core 3.x, or stay on 2.x (espressif32@6.9.0)?
- GPIO46 (backlight) is a strapping pin: GPIO46=1 with GPIO0=0 at reset is invalid — keep BL low at reset.
- Faster path later: draw straight into the RGB driver's own frame buffer (skip the 24 ms copy)?
