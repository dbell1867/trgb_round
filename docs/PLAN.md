# T-RGB round display — plan

## Resume here
- **Board:** LilyGO T-RGB **2.8" full circle**, touch GT911 @ 0x5D (confirmed).
- **Done:** detected (303a:1001, /dev/ttyACM0), serial perms OK, PlatformIO 6.1.19,
  researched board (memory: board-lilygo-t-rgb), **factory flash backed up + verified** (`backup/`).
- **Scaffolded** (approach A): `platformio.ini` (espressif32@6.9.0 = Arduino core 2.0.17,
  vendor board JSON in `boards/`), `include/board_pins.h`, `scripts/read_serial.py`.
- **Stage 1 DONE** (hello serial; lesson-01, snapshot `stages/01-hello-serial/`).
  Factory demo is gone from flash (restore from `backup/`).
- **`src/main.cpp`:** Stage 8 — stage 7 jack-o'-lantern eye (two-core pipeline, ~20 fps) that glares at the finger while in pain.
- **Stage 2a DONE — first light** (vendor lib + pushColors colour bars; colours + orientation correct;
  GT911 confirmed at 0x5D). lesson-02, snapshot `stages/02a-first-light/`.
- **Stage 2b DONE — text** (Arduino_GFX canvas in PSRAM → pushColors; strip updates 1.6 ms vs 24 ms full;
  no panel offset, r=239 ring fully visible). lesson-02, snapshot `stages/02b-text/`.
- **Stage 3a DONE — touch** (raw GT911 coords = screen pixels; ~84 Hz; ~9 empty polls/frame; multi-touch OK;
  thick-brush fix for 1-px-line "gaps"). lesson-03, snapshot `stages/03a-touch-paint/`.
- **Stage 3b DONE — touch INT** (GPIO 1 = LOW while touched OR report pending, latched until read; no
  per-report pulses; LEVEL mode = 0 idle reads, 81 Hz touched). lesson-03 §3b, `stages/03b-touch-int-level/`,
  data `docs/data/stage3b-int-modes.csv`.
- **Stage 4a DONE — LVGL 8** via vendor glue: +325 KB flash, no button bounce (taps + 4 s hold clean),
  arc drag 7–9 ms/redraw, ≤31/s (capped by 30 ms touch read). lesson-04, `stages/04a-lvgl-arc-button/`.
- **Stage 5a DONE — monster eye first light** (lesson-05): tables from `scripts/gen_eye.py`
  (`include/eye_dragon.h`), 16.4 fps (render 37 ms, push 24 ms). Found + fixed **USB serial hang**:
  core 2.0.17 `HWCDC::write` with `setTxTimeoutMs(0)` blocks forever when the port is closed but the cable
  is plugged in → use `logf()` (drops lines that don't fit). User confirmed it animates with no monitor open.
  Snapshot `stages/05a-eye-first-light/`.
- **Stage 5b DONE — saccades + pupil random walk** (non-blocking keyframe version of `split()`); 16 fps, 0.54 saccades/s,
  no gap at the rim. `stages/05b-eye-saccades/`.
- **Stage 5b2 DONE** — saccades tuned for 16 fps (glances, distance-scaled flicks, drift). `stages/05b2-eye-natural-motion/`.
- **Stage 5c DONE** — lids track + blink (close 100–150 ms, reopen half speed); 18 fps (lids skip lookups).
  `stages/05c-eye-lids/`.
- **Stage 5d DONE — touch = pain** (LEVEL-mode touch; flinch → squint + flutter, pinpoint pupil, tremble, red tint 3 s,
  recover 2 s; re-touch restarts). User: "all phases work". `stages/05d-eye-pain/`.
- **Stage 6 DONE — pumpkin lids** (lesson-06, `scripts/gen_lids.py` + PC preview, `stages/06-pumpkin-lids/`). Tables in
  internal RAM: 41.2 → 29.4 ms render (lid maps were the big win).
- **Stage 7 DONE (negative result) — two-core pipeline:** 19.6 → ~20.5 fps only; render and push each ~2× slower together
  → shared MSPI bus (PSRAM + flash + LCD scanout ≈ 90 MB/s) is the limit, not CPU. lesson-07, `stages/07-two-core-pipeline/`.
  Fixed task-watchdog reboot (push task starved IDLE0) with vTaskDelay(1); soak 121 s clean. User: leave as is for now. Parked idea: "race the beam" (render into driver fb found by scanning panel struct,
  start at VSYNC, write back cache per band; ~27 fps, no tearing) — or core 3 / IDF 5 double-buffered panel.
- **Stage 8 DONE — eye follows finger** (lesson-08, `stages/08-eye-follows-finger/`): touch = pain + eye locks on the
  finger with smooth pursuit (exp filter, TAU 60 ms); `GAZE` +1 glare / -1 look away. User: all four checks pass.
  Found a 2nd HWCDC trap: TX interrupt stalls with bytes queued, `logf()` guard never writes again → serial silent;
  fixed by kicking the transmitter after 1 s of drops (confirmed: kicks 1, free 84).
- **Next (resume here) — ideas, user to pick:** race the beam (see stage 7); battery + deep sleep with touch wake (read until INT HIGH);
  other eye designs via gen_eye.py; SD / Wi-Fi.

## Board facts (measured)
- ESP32-S3 (QFN56) rev v0.1, 8 MB embedded octal PSRAM, 16 MB Winbond flash (quad), MAC 34:85:18:a9:7a:58.

## Decisions
- **Approach A:** build on LilyGO's library at their pinned versions (core 2.x, LVGL 8);
  learn by reading `LilyGo_RGBPanel.cpp`. Reason: panel init goes through the XL9555 expander with
  per-variant command tables — a wrong guess is a silent black screen. Revisit B (Arduino_GFX/core 3.x) later.
- Stage 1 has no lib_deps, so any failure is the toolchain, not a library.
- Factory backup via chunked reader with ROM fallback (G42) — one-shot read fails deterministically.

- Drawing: Arduino_GFX `Arduino_Canvas(480,480,nullptr)` off-screen + vendor `pushColors`; push dirty strips only.

- Touch: no coordinate transform; always pass 5-element arrays to getPoint (SensorLib internals).
- Touch reads gated on INT LOW (LEVEL mode); edge-triggered IRQ doesn't work on this board.
- Serial logging via `logf()` (non-blocking, drops when full) — `setTxTimeoutMs(0)` alone hangs on core 2.0.17.
  `logf()` also re-kicks the USB transmitter after 1 s of drops (stage 8 stall).
- Keep CORE_DEBUG_LEVEL=0: level 3 + setDebugOutput broke GT911 detection (timing-fragile init).

- UI: LVGL 8.4 with the vendor's LV_Helper + lv_conf.h unmodified (no second lv_conf.h).

## Open questions
- Does the vendor library work on Arduino core 3.x, or stay on 2.x (espressif32@6.9.0)?
- GPIO46 (backlight) is a strapping pin: GPIO46=1 with GPIO0=0 at reset is invalid — keep BL low at reset.
- Faster path later: draw straight into the RGB driver's own frame buffer (skip the 24 ms copy)?
- Touch detection is timing-fragile (failed under debug logging). Test cold power-ups (unplug/replug ×N).
- Is GPIO 1 the raw GT911 INT or conditioned by Q25/diode? (Behaviour is measured; topology unverified.)
