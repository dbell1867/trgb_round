# Lesson 04 — LVGL 8 on the round display

**Board:** LilyGO T-RGB 2.8" · **Date:** 2026-10-11 · **Code:** `stages/04a-lvgl-arc-button/`

## Objectives
1. Read the vendor's LVGL glue and config before using them.
2. A round-friendly UI: arc gauge, big value label, a button.
3. Measure: flash cost, redraw cost, and whether touch "empty polls" cause button bounce.

## Reading first
**`LV_Helper.cpp` (LVGL 8 path), `beginLvglHelper(panel)`:**
- `lv_init()`, **two full-screen draw buffers** via `ps_malloc` (2 × 460 KB, PSRAM) — plus the RGB
  driver's own 460 KB frame buffer → ~1.4 MB of the 8 MB PSRAM.
- Flush callback: `pushColors(x1, y1, x2 + 1, y2 + 1, …)` — **end coordinates, handled correctly**
  (the trap from lesson 02).
- Touch callback: `getPoint(&x, &y, 1)` — 1-element storage. Safe only because no swap/mirror is
  enabled (lesson 03: SensorLib's `updateXY` writes per detected finger). **Never enable a touch
  transform with this glue.**
- No touch → `LV_INDEV_STATE_REL` (LVGL 8 pre-fills the last point, so release position is kept).

**`lv_conf.h` (vendor's, used unmodified):**
| Setting | Value | Meaning |
|---|---|---|
| `LV_COLOR_DEPTH` / `LV_COLOR_16_SWAP` | 16 / 0 | RGB565, no byte swap — same as our pushColors bars |
| `LV_MEM_CUSTOM_ALLOC` | `ps_malloc` | LVGL objects live in PSRAM |
| `LV_DISP_DEF_REFR_PERIOD` | 10 ms | redraw check every 10 ms |
| `LV_INDEV_DEF_READ_PERIOD` | 30 ms | touch read every 30 ms |
| `LV_TICK_CUSTOM_SYS_TIME_EXPR` | `millis()` | LVGL's clock |
| `LV_ASSERT_HANDLER` | `while(1);` | a failed LVGL assert **hangs** (no reboot) — G15 |
| Fonts | Montserrat 8–48 all enabled | linker drops unused ones |

**Decision:** don't add our own `lv_conf.h` — two configs in one build could compile LVGL and the
vendor glue with different settings (a silent mismatch).

**Revised prediction before measuring:** the "released on every empty poll" worry from lesson 03
shouldn't bite — LVGL reads every 30 ms and the GT911 posts a report every ~12 ms, so a fresh
report is almost always waiting.

## Instrument: LVGL's refresh monitor
The glue's driver struct is private, but `lv_disp_get_default()->driver->monitor_cb = onMonitor;`
reaches it. LVGL calls it after every redraw with **time (ms)** and **pixels redrawn**.

## Results
| Prediction | Measured | |
|---|---|---|
| Flash ~5 % → ~10 % (+~300 KB) | 5.1 % → **10.1 %** (+325 KB) | ✅ |
| 5 taps → 5 / 5 / 5, no extras | pressed 5, released 5, clicked 5, in order | ✅ no bounce |
| 3 s hold → pressed 1, long 1, released 1, clicked 1 | exactly that (~4 s hold, ~130 touch reads) | ✅ |
| Arc-drag redraw 20–60 ms (~20–40 fps) | **avg 7–9 ms, max 10 ms, up to 31 refreshes/s** | ✗ far better |
| — | first full-screen render 66 ms (230 400 px) | |
| Arc smooth, no flicker (user) | smooth | ✅ |

### Why the arc prediction was wrong (twice)
1. **Area:** each refresh averaged ~12 500 px (390 206 px ÷ 31) — LVGL invalidates only the changed
   slice of arc + the label, not the whole ring. I'd costed the whole ring.
2. **Rate ceiling is input, not rendering:** touch read every 30 ms → ≤ 33 new positions/s → ≤ 33
   redraws/s. Rendering (~8 ms) has headroom. To make drags feel snappier, lower
   `LV_INDEV_DEF_READ_PERIOD` (but then empty polls *could* start causing releases — lesson 03).

## C++ / LVGL for Python people
| C++ / LVGL | Python analogue |
|---|---|
| `lv_obj_t *btn = lv_btn_create(parent);` | `btn = Button(parent)` — a handle, owned by LVGL |
| `lv_obj_add_event_cb(btn, onButton, LV_EVENT_ALL, nullptr)` | `btn.bind("<all>", on_button)` (Tkinter-style) |
| `switch (lv_event_get_code(e)) { case …: … break; }` | `match code: case …:` — `break` stops fall-through |
| `lv_scr_act()` | the root window |
| `lv_disp_get_default()->driver->monitor_cb = f;` | monkey-patching a hook onto an object you didn't create |
| `nullptr` | `None` for pointers |

## Cheat-sheet
```cpp
#include <LV_Helper.h>
panel.begin();
beginLvglHelper(panel);                       // display + touch wired into LVGL
lv_disp_get_default()->driver->monitor_cb = onMonitor;   // (ms, px) per redraw
/* build widgets on lv_scr_act() */
lv_refr_now(nullptr);  panel.setBrightness(16);           // draw first, then light (G21)
void loop() { lv_timer_handler(); delay(2); }
```
