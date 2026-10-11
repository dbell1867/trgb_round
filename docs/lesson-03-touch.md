# Lesson 03 — Touch (GT911)

**Board:** LilyGO T-RGB 2.8" (GT911 @ 0x5D) · **Date:** 2026-10-11 · **Code:** `stages/03a-touch-paint/`

## Objectives
1. Read how touch reaches us (vendor lib → SensorLib) before using it.
2. Measure orientation instead of guessing (G8).
3. Measure the real sample rate and polling cost (G12).
4. A finger-painting demo: continuous strokes, multi-touch, BOOT clears.

## Reading the source
- `panel.getPoint(x[], y[], n)` → for the GT911, straight to SensorLib 0.2.3 `TouchDrvGT911::getPoint`
  (the IRQ gating in the vendor code applies only to the FT3267).
- `TouchDrvGT911::getPoint`: reads status reg `0x814E`, **clears it**, returns 0 if no points;
  otherwise reads 39 bytes from `0x814F` (5 points × 8 bytes) → **an empty poll is cheap**.
- **Two latent memory-safety traps** (C++ has no `IndexError` — out-of-bounds writes corrupt memory silently):
  - it copies into a fixed `p[5]` for `i < size` → asking for > 5 points overflows;
  - `updateXY()` loops over *fingers detected*, not the size of the caller's arrays → with a transform
    enabled, 1-element arrays + 2 fingers would write past them.
  - **Rule:** always pass 5-element arrays.
- Transforms (swap/mirror) default **off** → raw GT911 coordinates. LilyGO's LVGL glue feeds them
  to LVGL untransformed, and reports "released" on every empty poll (relevant for the LVGL stage).

## Design
Five crosshair targets at known pixels (T 240,40 · R 440,240 · B 240,440 · L 40,240 · C 240,240);
finger 0 paints a trail, extra fingers paint coloured dots; only changed rows pushed; BOOT clears;
one summary line per stroke (G9). Stroke ends after 60 ms with no frame.

## Results
### Orientation (G8) — prediction: raw = screen pixels ✅
| Target | Reported | Error |
|---|---|---|
| T (240,40) | (242,29) | 2, −11 |
| R (440,240) | (437,238), (446,240) | ≤ 6 |
| B (240,440) | (228,442) | −12, 2 |
| L (40,240) | (43,244) | 3, 4 |
| C (240,240) | (226,243) | −14, 3 |

All within a fingertip (~14 px). Right raises x, down raises y → **no swap, no mirror, no scaling.**

### Rate and polling cost (G12)
| | Predicted | Measured |
|---|---|---|
| Frame rate | ~100 Hz (60–120) | **69–97 Hz, typically ~80–84** ✅ |
| Empty polls per frame | "several" (estimated ~3) | **~7–9** (3910 / 444 frames) ✅ held, number off |

Estimate was off because I costed every poll as a full read (~1.5 ms). An empty poll returns after
the status read + clear (~0.25 ms); with `delay(1)` we poll every ~1.3 ms → ~9 empties per ~12 ms
frame. **~90 % of touch I²C transactions are wasted**, on a bus shared with the expander and Grove.

### Multi-touch, BOOT
`max_fingers=2` on two-finger touches, second finger drew red dots (user). BOOT → `cleared`, screen redrawn.

## Bug: "gaps" in fast strokes — a hypothesis falsified by eye
- **Observation:** gaps between dots on fast swipes, though every sample is joined to the previous one.
- That contradicts the model, so list hypotheses before changing code:
  1. visual — 1 px `drawLine` between 9 px dots looks like beads on a string;
  2. strokes really break (GT911 silent > 60 ms → new stroke, no join) — would show as extra `stroke:` lines;
  3. the line's rows aren't pushed.
- **Probe:** look closely at a gap. User: *"a faint thin line between the dots"* → **hypothesis 1**.
- **Fix:** `drawThickLine()` stamps radius-4 circles every ~2 px along the segment → solid trail (user confirmed).
- Lesson: the interpolation (G12) was right; the brush wasn't. Checking by eye was cheaper than any code change.

## C++ for Python people
| C++ | Python analogue |
|---|---|
| `struct Target { int x, y; const char *name; };` | `@dataclass class Target: x: int; y: int; name: str` |
| `for (const Target &t : targets)` | `for t in targets:` (`&` = no copy, `const` = read-only) |
| `static` local variable in `loop()` | state kept between calls, like an attribute on `self` |
| `int16_t xs[5]` | fixed-size array — writing `xs[5]` is *undefined behaviour*, not an exception |
| `x0 + dx * i / steps` | integer maths: `/` truncates like `//` (toward zero for negatives) |

## Cheat-sheet
```cpp
int16_t xs[5], ys[5];                       // always 5 (SensorLib GT911 internals)
uint8_t n = panel.getPoint(xs, ys, 5);      // 0 = nothing new (between ~84 Hz frames, or no finger)
// raw coords = screen pixels on the 2.8" — no transform needed
```

## Open / next
- **3b:** use the GT911 **INT** line (GPIO 1) so we read only when a frame is ready — measure I²C
  transactions per frame before/after. LilyGO warns "do not modify the touch interrupt mode".
