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

---

# Stage 3b — The touch INT line (GPIO 1)
**Code:** `stages/03b-touch-int-level/` · **Data:** `docs/data/stage3b-int-modes.csv`

**Goal:** stop wasting ~90 % of touch I²C reads by using the GT911 INT line.
**Outcome:** the model I started with was wrong three times; the measured model gives
**zero I²C traffic when idle** and the same 81 Hz while touched.

## Reading first
- SensorLib drives INT **LOW while releasing reset** (via the expander) → the GT911 latches
  address **0x5D** (INT HIGH would give 0x14). INT has two jobs.
- The interrupt mode lives in the GT911's own config (`0x804D`); `setInterruptMode()` rewrites
  and reloads that config — that's what LilyGO's "do not modify the interrupt mode" warns about.
- SensorLib logs the chip's self-description at `log_i` level — free information.

## Detour 1: turning on logging broke touch detection
- `CORE_DEBUG_LEVEL=3` + `Serial.setDebugOutput(true)` → GT911 "Product id:0", no touch chip
  found → `assert(_init_cmd)` → boot loop (3/3 boots). Final I²C scan showed the GT911 at **0x14**
  (the FT6X36 probe reset it last with INT floating).
- Level 3 *without* `setDebugOutput` (slower, ~7 ms/line) → works (2/2). Level 0 → works (every boot).
- So it isn't "more delay breaks it" — the reset/address handshake is sensitive to *where* delays
  fall (SensorLib's own comment: "if the time is too long, it will not be accessible").
  LilyGO's auto-detect resets the touch chip **4 times** (CST816, CST226, CST92xx, GT911) before success.
- **Open issue:** touch detection is timing-fragile. Test cold power-ups later.

## Detour 2: "Resolution Y = 224"
- Logged `X = 480, Y = 224` — impossible, we'd measured y = 442. 480 = 0x01E0, 224 = 0x00E0 → missing high byte.
- Datasheet: `0x8146–0x8149` *are* the read-only coordinate-resolution registers (code correct);
  `0x8048–0x804B` are the "Output Max" config.
- Raw read at level 0: **both 480 × 480.** My hypothesis ("the info field is stale") was wrong —
  it was a **bad byte read** during the fragile logged init (SensorLib reads byte by byte).

## The experiments
| Probe | Prediction | Result |
|---|---|---|
| GT911 interrupt mode (log) | unknown | `FALLING` → watch falling edges |
| Refresh rate / max points (log) | 10–12 ms / 5 | 10 ms / 5 ✅ |
| INT idle level | HIGH (falling mode) | **LOW** ✗ |
| INT idle under no pull / pull-up / pull-down | LOW/HIGH/LOW (floating, open-drain) | **LOW/LOW/LOW** ✗ — actively driven |
| Our 1 kHz I²C polls cause the idle blips? | maybe | ✗ — 50 s of POLL idle, 0 edges; blips were start-up only |
| Edges per held-touch second | ~84 (one per report) | **0** ✗ — 1 change at touch-down, a burst at lift |
| IRQ (edge-triggered reads) | reads ≈ edges ≈ 84/s | **dead** — no edges during a touch |
| INT LOW % while held (sampled) | ~100 % | **100 %** ✅ |
| LEVEL mode idle reads | ~0 | **0** (vs 1000/s POLL) ✅ |
| LEVEL mode held frames | ~80 | **81** ✅ |
| NOREAD: after a tap, does INT latch? | latch | **LOW for 14 s and 29+ s**, until the first read (which returned a stale frame) ✅ |

Taps: POLL ~985 reads/s vs LEVEL 33–44 reads/s (**~96 % fewer**). A tap chatters 7–26 edges
(µs blips, min gap 2 µs) — another reason not to trust single edges.

### Instrument lesson
The ISR classified edges by reading the pin *after* entering the interrupt. With 2 µs pulses
the pin can already have changed back — that classification couldn't be trusted. Fixes: count
raw changes and the min gap between edges (needs no pin read), and **sample the level** in
`loop()` (% LOW per second). Design probes that *can* disagree (G29): the pull-up/pull-down
test could have shown "floating" — it showed "driven".

## Final model
**On this board GPIO 1 is LOW while a finger is on the glass OR an unread report is pending.**
The GT911 holds its last report and keeps INT LOW until it's read; it does **not** pulse per report.

- LOW at boot = report left pending by init, cleared by our first read.
- **LEVEL mode** (read while LOW, one more after it rises): zero idle traffic, full rate when touched.
- **Deep sleep consequence:** wake = INT LOW, so a pending report would wake the chip at once.
  Read until INT is HIGH before sleeping — exactly what LilyGO's `sleep()` does.
- Still ~9 empty polls per frame *while* touching (INT doesn't mark individual reports). If that
  matters: read every ~10 ms while LOW instead of every loop.

(Hypothesis, not verified: the schematic shows a PNP transistor (Q25) and diode near `TP_INT`,
so GPIO 1 may see a conditioned version of the GT911 pin. The behaviour above is what the ESP32 sees.)

## C++ for Python people
| C++ | Python analogue |
|---|---|
| `void IRAM_ATTR onTouchInt()` + `attachInterrupt(pin, fn, CHANGE)` | a callback the *hardware* calls mid-`loop()` — like a signal handler |
| `volatile uint32_t count;` | "don't cache this — it changes behind your back" |
| `portENTER_CRITICAL(&mux)` … `EXIT` | a `threading.Lock()` that also blocks interrupts on this core |
| `enum class Mode { POLL, LEVEL, NOREAD };` | `class Mode(Enum): ...` |
| `cond ? a : cond2 ? b : c` | `a if cond else b if cond2 else c` |

## Cheat-sheet
```cpp
// Touch on the T-RGB 2.8": read only while INT (GPIO 1) is LOW.
static bool wasLow = false;
bool low = digitalRead(1) == LOW;
if (low || wasLow) { n = panel.getPoint(xs, ys, 5); /* ... */ }
wasLow = low;
// Before deep sleep with touch wake: getPoint() until digitalRead(1) == HIGH.
```
