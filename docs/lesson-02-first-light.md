# Lesson 02 — First light (vendor library + pushColors)

**Board:** LilyGO T-RGB 2.8" (GT911) · **Date:** 2026-10-11 · **Code:** `stages/02a-first-light/`

## Objectives
1. Read how the vendor library brings the panel up *before* using it.
2. Get pixels on the screen with the smallest possible stack (no graphics library).
3. Confirm geometry, colour order and orientation by eye.

## Reading the driver first (`LilyGo_RGBPanel.cpp` @ 359c03f)
`begin()` → `initDevice()`:
1. Backlight GPIO 46 **LOW** (stays dark until we call `setBrightness`) — also keeps the GPIO46
   strapping pin low around reset.
2. `extension.init(Wire, 8, 48)` — starts **`Wire`** and the XL9555. *The library owns `Wire` from here.*
3. XL9555 IO2 (**power enable**) HIGH.
4. `initTouch()` — tries CST8xx, then GT911, then FT3267; the one that answers picks the panel init table.
5. `initBUS()` — pulse LCD reset (expander IO6), then send the ST7701S init table over **3-wire SPI
   bit-banged through the expander** (I²C raised to 1 MHz for this, back to 400 kHz after).
6. `esp_lcd_new_rgb_panel()` — ESP-IDF RGB peripheral, frame buffer in PSRAM (`fb_in_psram = 1`).

Other findings:
- **Drawing API is one function:** `pushColors(x, y, w, h, data)`. Lines/text need a library on top.
- **`setBrightness(0..16)` is pulse-count dimming**, not PWM: each LOW→HIGH pulse on GPIO 46 steps
  the backlight driver down one of 16 levels.
- **16 of 18 data lines used:** panel is wired RGB666, ESP sends RGB565; R0/B0 (GPIO 43/44 = UART0)
  unused — why serial falls back to GPIO 43/44 when USB CDC is off.
- Timing: PCLK 8 MHz, frame = (480+30+50+1)×(480+30+20+1) = 297 891 clocks → **≈27 Hz refresh** (calculated).
- The header `#error`s on SensorLib ≥ 0.3 — the version pin is mandatory.

## Dependencies
`lib_deps`: T-RGB @ `359c03f`, SensorLib 0.2.3, lvgl 8.4.0. lvgl isn't used yet but PlatformIO
compiles every file of a library, and `LV_Helper.cpp` includes it. **Surprise:** GFX Library for
Arduino and TFT_eSPI appeared too — the vendor's `library.json` declares its own dependencies.
Checked: all four are pinned there to the same versions we chose.

## Stage 2a — colour bars
One 480×480 RGB565 frame (460 800 B) from `ps_malloc`, four vertical bars R·G·B·W, a yellow
square top-centre (round screen hides corners → markers go near the centre line), one
`pushColors`, then backlight on (G21).

| Prediction | Measured | |
|---|---|---|
| `begin()` ≈ 2 s | 2302 ms | ✅ |
| model 3 / GT911 | `model=3 touch=GT911` | ✅ touch chip confirmed |
| I²C: 0x20 + 0x5D/0x14 | `0x20 0x5D` only | ✅ |
| buffer in PSRAM (0x3C…/0x3D…) | `0x3d8711dc` | ✅ |
| `pushColors` ≈ 10 ms (5–30) | 23.9 ms | ✅ range; 2.4× point estimate |
| R·G·B·W, yellow at top | exactly that, no flicker/offset (user) | ✅ colour order + orientation correct |

**Why 23.9 ms:** a PSRAM→PSRAM copy of 460 KB ≈ 19 MB/s, while the LCD DMA is also streaming the
frame out of PSRAM ~27×/s on the same bus. A full-frame redraw costs ~24 ms of copying alone —
the reason UI libraries redraw only dirty regions.

## C++ for Python people
| C++ | Python analogue |
|---|---|
| `LilyGo_RGBPanel panel;` then `panel.begin()` | `panel = LilyGo_RGBPanel(); panel.begin()` — object, so `.` |
| `begin()` / `begin(type)` (overloads) | one function with an optional argument |
| `(uint16_t *)ps_malloc(n)` | `bytearray(n)` in a chosen memory; the cast says "treat as 16-bit pixels" |
| `frame[y * W + x]` | a flat list indexed like a 2-D grid |
| `RED \| GREEN` | bitwise OR, same as Python |

---

## Stage 2b — text (Arduino_GFX canvas → pushColors)
**Code:** `stages/02b-text/`

Design: `Arduino_Canvas(480, 480, nullptr)` is a pure in-memory surface (its `begin()` skips the
output when it's null; pass `GFX_SKIP_OUTPUT_BEGIN`). We draw with Arduino_GFX, then copy
with the vendor's `pushColors`. Round-screen geometry probe: rings at r = 239 (red), 232
(green), 225 (blue). Live uptime counter updates only a 32-row strip.

### Bug 1 (caught before compiling): macros vs `constexpr`
Arduino_GFX does `#define RED RGB565_RED`. Our `constexpr uint16_t RED = 0xF800;` would have
been rewritten by the preprocessor into `constexpr uint16_t RGB565(255,0,0) = 0xF800;` — nonsense.
A macro is search-and-replace on the source *before* compiling and ignores scope. Fix: use
the library's `RGB565_*` names. (This is why modern C++ prefers `constexpr` to `#define`.)

### Bug 2 (real crash): the header's parameter names lie (G26)
```
assert failed: rgb_panel_draw_bitmap esp_lcd_rgb_panel.c:306
  ((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position")
Rebooting...   (boot loop)
```
- Full-frame `pushColors(0, 0, 480, 480, …)` worked; strip `pushColors(0, 296, 480, 32, …)` crashed.
- **Hypothesis:** the last two args are *end* coordinates, not width/height — `(0,0,480,480)` is
  the same either way, which is why the full frame worked.
- **Source:** `pushColors(x, y, width, hight, data)` passes them straight to
  `esp_lcd_panel_draw_bitmap(x_start, y_start, x_end, y_end)` (end exclusive). LilyGO's own LVGL
  glue calls `pushColors(x1, y1, x2 + 1, y2 + 1, …)`. Hypothesis confirmed.
- **Fix:** `pushColors(0, y, W, y + h, fb + y * W)`, wrapped in `pushStrip()` with a warning comment.
- An **assert** = a built-in sanity check; on a microcontroller a failed one prints and reboots.

### Results
| Prediction | Measured | |
|---|---|---|
| Canvas `aligned_alloc` lands in PSRAM (or fails — only 370 KB internal) | `0x3d8711f0` | ✅ |
| Strip push = 32/480 of 24 ms ≈ 1.6 ms | 1.44–1.69 ms | ✅ (~15× cheaper than full frame) |
| Rings: is r = 239 visible? (no expectation — a probe) | all three fully visible, gap even all round (user) | **no panel offset; full 480 px circle usable, centre (240,240)** |
| Text centred, uptime smooth | yes (user) | ✅ |

## Cheat-sheet
```cpp
LilyGo_RGBPanel panel;  panel.begin();                 // power, expander, touch detect, panel init (~2.3 s)
Arduino_Canvas *canvas = new Arduino_Canvas(480, 480, nullptr);
canvas->begin(GFX_SKIP_OUTPUT_BEGIN);                  // buffer in PSRAM
uint16_t *fb = canvas->getFramebuffer();
panel.pushColors(x0, y0, x_end, y_end, fb_ptr);        // END coords (exclusive), not w/h!
panel.pushColors(0, y, 480, y + h, fb + y * 480);      // full-width strip = contiguous
panel.setBrightness(0..16);                            // after drawing (G21)
```

## Glossary
- **Canvas / off-screen buffer** — draw into memory first, then copy to the display in one go (no half-drawn frames).
- **Dirty region** — only the part of the screen that changed; pushing just that is much cheaper.
- **Macro (`#define`)** — preprocessor text substitution before compiling; no types, no scope.
- **assert** — a runtime check that halts (here: reboots) when an assumption is false.
