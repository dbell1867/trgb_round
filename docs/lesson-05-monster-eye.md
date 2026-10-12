# Lesson 05 — Monster eye (Uncanny Eyes port)

**Goal:** an animated dragon eye like Adafruit's Halloween *Uncanny Eyes*, which blinks
"in pain" when touched and then recovers. Built in stages: 5a render a still eye → 5b motion →
5c blinks + eyelid tracking → 5d touch → pain.

Credit: renderer, table format and dragon art are from Adafruit's
[Uncanny_Eyes](https://github.com/adafruit/Uncanny_Eyes) (MIT license, Phil Burgess / Paint Your Dragon).

## How Uncanny Eyes draws an eye (no 3D, just lookup tables)
| Table | What it is |
|---|---|
| `sclera` | RGB565 image of the eyeball (black for the dragon) — panned to move the eye |
| `iris` | polar texture, 512 angles × 80 radii — wrapped around the pupil |
| `polar` | per iris pixel: angle (high 9 bits) + distance from the iris edge (low 7 bits) |
| `upper`, `lower` | eyelid maps: pixel covered if `map <= threshold` (0 = open, 254 = shut) |

The **pupil is wherever the sclera shows through**: an iris pixel is drawn only if its distance
from the edge is below a threshold, so changing that threshold resizes the pupil, and the dragon's
pupil *map* makes the hole a slit.

## Stage 5a — first light
`scripts/gen_eye.py` (port of Adafruit's `tablegen.py`) builds the tables at **240 px** from the
128 px art (Lanczos/bilinear/bicubic upsampling). Checked at 128 px: all five tables byte-identical to
Adafruit's `dragonEye.h` (symmetrical lids — the shipped asymmetrical PNGs don't match their header).
The eye is rendered at 240×240 and pixel-doubled to 480×480 (each pixel written as one 32-bit word
= two pixels, then each row `memcpy`'d to the row below), then pushed with `pushColors`.

| | Predicted | Measured |
|---|---|---|
| render + doubling | 15–25 ms | **~37 ms** ✗ |
| push (full frame) | ~24 ms | **24.0 ms** ✓ |
| frame rate | 20–25 fps | **16.4 fps** ✗ |
| eyelids visible | yes | **no** ✗ — thresholds were 0 = fully open; Adafruit's lids show because tracking/blinks move them (5c) |

Looks right: fiery iris, black slit pupil, no colour/orientation problems.

## Bug: "the eye freezes" — it was the USB serial port
**Symptom:** the pupil breathed for a few seconds after reset, then the picture froze — while every
serial log I read said the firmware was running at 15–16 fps.

**Probes (each designed to be able to disagree):**
1. Panel timing — counted VSYNC/DE edges by enabling the input buffer on pins the LCD drives:
   5–6 VSYNC and ~2,600 DE per 200 ms = 26.9 Hz exactly as predicted. Not a stalled scanout.
2. Pixel data — counted toggles on GPIO 2 (red MSB): ~300 with a narrow pupil, ~75 wide, tracking
   the pupil for 44 s. Fresh frames *were* leaving the chip.
3. Kicked the ST7701S (SLPOUT/DISPON) every 10 s.

Then the user's observation broke it open: **the screen moved while I was probing and froze as soon
as I stopped.** Every "the firmware is running" measurement had been taken with the port *open*.
Reopening the port without a reset showed the log jumping **t=44 s → t=124 s**: `loop()` had been
stuck for 80 s and resumed the instant something read the port. The display simply kept scanning out
the last frame.

**Root cause** (core 2.0.17, `cores/esp32/HWCDC.cpp`, `HWCDC::write`):
```cpp
uint32_t tries = tx_timeout_ms;   // we set 0 via setTxTimeoutMs(0) (G11)
while (connected && to_send) {    // "connected" = USB SOF packets seen — true whenever the
    ...                           //  cable is plugged in, even with no program on the port
    if (last_toSend == to_send) { tries--; delay(1); }   // 0 - 1 wraps to 4,294,967,295
    if (tries == 0) { /* give up */ }                     // ~49 days away
}
```
Our ~150-byte log lines fill the 256-byte TX ring buffer in a couple of prints; the next write waits
for space that nobody will free. **`setTxTimeoutMs(0)` — meant to make printing non-blocking —
makes it block forever on this core.**

**Fix:** `logf()` formats into a local buffer and writes only if `Serial.availableForWrite()` can take
the whole line, so `write()` never enters the waiting loop; lines are dropped when nobody listens.
Verified: port closed for 30 s → frame counter went 165 → 701 over 32 s (16.75 fps), no stall.

**Lessons**
- An instrument that is only ever used in one condition can't see a bug that lives in the other
  condition. All my evidence was gathered with the port open — the one state where it worked.
- When the user's eyes and the logs disagree, ask *when* the observation was made.
- C++ unsigned integers wrap: `uint32_t x = 0; x--;` is 4294967295, not −1 (Python ints never wrap).

### C++ for Python people
- `va_list` / `vsnprintf` = how `printf`-style functions take `*args`; `vsnprintf` is like
  `fmt % args` into a fixed-size buffer and returns the length it wanted.
- `#define private public` before an `#include` exposes a class's private members — a probe-only hack
  (used for the panel kick, then removed).

## Stage 5b — the eye looks around
Ported from `frame()` in Uncanny Eyes:
- **Saccades:** hold 0–3 s → pick a random target inside a circle (positions 0..1023, rejection
  sampling) → flick there in 72–144 ms along an ease curve (smoothstep `3t² − 2t³`; the original uses a
  table of the same shape). Looking around = panning the 240 px window over the 300 px sclera:
  offset 0..60, centre 30 → ±60 px on screen. The iris (300 px) only just covers the screen at the
  extremes — confirmed by eye: no black gap at the rim.
- **Pupil random walk:** Adafruit's recursive `split()` (midpoint displacement, range halved per level
  until < 8 → 64 segments per 10 s target). The original renders frames *inside* the recursion, which
  blocks `loop()`; here the recursion fills a keyframe list and `loop()` interpolates by time —
  same motion, non-blocking (needed for touch in 5d).

| | Predicted | Measured |
|---|---|---|
| frame rate | ~16.4 fps | 15.8–16.9 fps ✓ |
| saccades | ~0.6 /s (mean hold 1.5 s + flick 0.1 s) | 15 in ~28 s = 0.54 /s ✓ |
| offsets | spread in 0..60 | 9..58 ✓ |
| flick look | 1–2 frames → jumps rather than glides | as predicted; looks acceptable |

**Observation:** render time follows pupil size — ~38.9 ms with a wide pupil vs ~35.2 ms narrow. Pupil
pixels read the 180 KB `sclera` table, iris pixels the 80 KB `iris` table; the bigger flash table
misses the cache more. A lead for optimising later.

### C++ for Python people
- `struct Saccade { ... };` with member functions ≈ a small Python class; members get default values
  in the declaration (`bool moving = false;`) like dataclass defaults.
- Recursion works exactly as in Python; `split()` appends to a fixed-size array (`value[count++]`)
  instead of a list, so it carries a `count < MAX_KEYS - 1` guard — C arrays don't grow or bounds-check.
- `int v = a + (b - a) * (int64_t)dt / span;` — the cast to 64-bit avoids overflow, since
  `320 × 10,000,000 µs` doesn't fit in 32 bits (Python ints never overflow).

## Stage 5b2 — less jerky at 16 fps
Feedback: "the jumps are a bit jerky". Cause: a frame every ~60 ms vs 72–144 ms flicks → 0–1
in-between frames, so the eye teleports (the original runs at 60+ fps on small screens).
Changes (motion only — frame rate untouched):
- **Mostly glances:** 80 % of targets are a hop of ≤ 160 units from the current position, 20 % a
  look anywhere in the circle.
- **Duration grows with distance** (real eyes' "main sequence"), stretched for 16 fps:
  `100 ms + 0.2 ms/unit`, ±15 % → ~110 ms tiny hop … ~400 ms full swing.
- **Drift while holding:** two slow unrelated sines per axis, ±25 units (≈ 3 screen px).
- Holds 0.2–3 s (no back-to-back flicks).

| | Predicted | Measured |
|---|---|---|
| mean flick | ~140 ms (glance ~125, big ~190) | 139 ms ✓ |
| glances : big | 80 : 20 | 14 : 3 ✓ |

Verdict by eye: "better now". The real fix is more frames — a two-core render/push pipeline is a
later stage.

## Stage 5c — eyelids: tracking + blinks
- **Tracking** (from `frame()`): sample the `upper` threshold map a quarter iris above the pupil
  (`sampleY = SCLERA/2 − (sy + IRIS/4)`, X influence halved, averaged with the mirror point). A map
  value is "the threshold at which this spot is covered", so using it as the threshold puts the lid
  edge just above the pupil. Smoothed `u = (3u + n)/4`; lower lid `l = 254 − u`.
- **Blinks:** OPEN → CLOSING → OPENING state machine; thresholds blended towards 254 (shut).

Predictions computed from the tables *before* running (a calculation, not a guess):

| | Predicted | Measured |
|---|---|---|
| upper threshold, looking up / centre / down | 15 / 59 / 104 | ~40 at offset 40, 85–101 at offset 5–11 ✓ |
| lid coverage | 33–49 % of the circle | — (looks right by eye) |
| render | 27–31 ms (covered pixels skip lookups) | 29.5–31.5 ms ✓ |
| frame rate | 18–19 fps | 18.0–18.7 fps ✓ |
| blinks | ~0.45 /s | 0.48 /s ✓ |

Lids track and edges are clean, but **blinks looked like "flashes of black"** — the 36–72 ms close
is 0–1 frames at 18 fps (same cause as the jerky saccades). A real human blink closes in ~100–150 ms
and reopens in ~150–300 ms, so **close 100–150 ms, reopen at half speed** is both more realistic and
2–3 + 4–5 frames here. Verdict: "blinks look natural now".

Pattern from 5b2 and 5c: timings tuned for a 60 fps screen must be re-tuned for the frame rate you
actually have — anything shorter than ~2 frames reads as a jump.

## Stage 5d — touch = pain
Everything derives from **time since the last touch**, through one `level` (1 while hurting, ramping
to 0 over the recovery) — so a new touch just resets the clock:

| Time after touch | Effect |
|---|---|
| 0–0.4 s | flinch: lids slam shut in 60 ms (deliberately a snap) and hold |
| 0.4–3 s | squint (lids 60 % shut) + flutter (150 ms dips every 0.25–0.45 s); pupil → pinpoint (iScale 30, below the normal minimum 80); tremble ±6 rendered px per frame; iris tinted 20/32 towards red |
| 3–5 s | recovery: level 1 → 0, all of the above eases back |

- **Touch:** stage 3b's LEVEL mode (read the GT911 only while INT is LOW, plus one read after it
  rises). INT latches until read, so a tap between two frames (~55 ms apart) is never missed; idle
  costs zero I²C traffic.
- **Lids refactor:** blinks and pain each give a *closure* 0..1; `closure = max(blink, pain)`, then
  `u = tracked + (254 − tracked) × closure` (and the same towards 254 for the lower lid).
- **Red tint:** `blend565()` spreads RGB565's fields apart in a 32-bit word
  (`(c | c << 16) & 0x07E0F81F`) so one multiply blends R, G and B at once. It relies on unsigned
  wrap-around when `f − b` is negative, so it was **checked on the PC first**: 100,000 random blends
  against a per-channel reference, worst error 1 step (rounding).

Result: "all phases work, red tint is good". 3 pain episodes counted, 17.4–18 fps.

### C++ for Python people
- `static bool wasLow = false;` inside a function keeps its value between calls — like a function
  attribute or a closure variable in Python.
- `min(1.0f, x)` — the `f` makes `1.0` a `float`; `min` needs both arguments the same type, and a
  bare `1.0` is a `double`.
- The tint is applied only in the iris branch: blending the black pupil with red would make it red too.
