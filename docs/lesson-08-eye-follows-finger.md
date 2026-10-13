# Lesson 08 — The eye glares at your finger

**Goal:** while it hurts, the jack-o'-lantern eye stops wandering and stares at the finger that
poked it — following the finger if it slides — then goes back to roaming once the pain is over.

## Behaviour
- A touch still triggers the stage 5d pain (flinch, squint, pinpoint pupil, tremble, red iris).
- The touch point becomes the eye's **target**. No saccades while locked; the eye chases the target.
- Finger lifted: the eye keeps staring at the last point until the pain ends (~5 s after the last
  touch), stares 0.5–1.5 s longer, then saccades resume from where it is.
- `constexpr int GAZE = +1;` — set it to `-1` and the eye looks *away* from the finger instead.

## Two ways an eye moves
Real eyes have two separate movement systems:
- **Saccades** — fast jumps between fixed points (stage 5b: eased flicks with distance-scaled time).
- **Smooth pursuit** — tracking a moving target. Here it's an exponential filter:

```cpp
float k = 1 - expf(-(float)(now - lastUs) / PURSUIT_TAU_US);   // TAU = 60 ms
lookX += (targetX - lookX) * k;
```
In Python terms: `look += (target - look) * k` every frame. A fixed `k` (say 0.5) would make the eye
faster at higher frame rates; computing `k` from the elapsed time makes it close 63 % of the gap
every 60 ms whatever the frame rate. At ~20 fps (50 ms frames) that's 57 % per frame — a quick snap
when the finger lands, then a lag of ~0.15 s behind a sliding finger.

## The sign trap
The iris never moves inside the sclera image — it's painted in the middle of it. `sx, sy` choose
*which part of the image the screen shows*. Show more of the image's left side (smaller `sx`) and
the iris appears further **right**. So the finger → eye mapping needs a minus sign:

```cpp
int fingerToEye(int t) { return constrain(512 - GAZE * (t - W / 2) * 512 / (W / 2), 0, 1023); }
```
Screen centre (240) → 512 (looking straight ahead); the rim → the eye's full range. The same formula
works for y (finger low → smaller `sy` → iris low). Travel is limited by the art: `SCLERA_SIZE − S` =
60 rendered px, so the iris moves at most ±60 screen px however far out the finger is.

## Predictions vs results
| Prediction | Result |
|---|---|
| tap near the right edge → after the flinch the iris points right | ✓ (user) |
| tap top / bottom → iris up / down, upper lid follows it down | ✓ |
| slide around the rim → follows smoothly, ~0.15 s behind | ✓ "follows smoothly" |
| lift → stares ~5 s, then roams again | ✓ |
| log: finger x=107 (left) → eye x > 512 | ✓ eye 795 |
| frame rate unchanged | ✓ ~20 fps roaming; 22–23 fps while squinting (more pixels are lid = cheaper) |

## Bug: serial goes silent (the animation doesn't)
The first test log stopped dead at the first touch — no more lines, ever, while the screen kept
animating. A fresh reader got nothing either. Reading `HWCDC.cpp` (core 2.0.17) showed why:

1. `write()` puts bytes in a 256-byte ring buffer, then **kicks** the hardware (flush the FIFO, enable
   the IN_EMPTY interrupt).
2. The interrupt sends up to 64 bytes, and **switches itself off** each time it runs; only `write()`
   switches it back on.
3. Our `logf()` (lesson 05) only calls `write()` when the whole line fits.

If the interrupt ever stops with bytes still queued, the free space never grows → `logf()` never
writes → nothing kicks the hardware → silent for good. A deadlock between the driver and our own
safety guard. Stage 8 made it likelier by making the log line longer (~120 → ~152 bytes): now just
105 stuck bytes block every line.

**Test:** after 1 s of dropped lines, `logf()` does the two-line kick itself (no bytes sent):
```cpp
usb_serial_jtag_ll_txfifo_flush();
usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
```
Prediction: logging pauses ~1 s after a stall and comes back, `kicks ≥ 1`, free space < 152.
```
[09:59:42] t= 50s  23.4 fps ... GLARE ...  kicks 0 (free 0)
                                   <- t=52 and t=54 lines missing
[09:59:44] t= 56s  20.1 fps ... roam  ...  kicks 1 (free 84)
```
✓ Stalled with 172 bytes stuck, one kick fixed it. (The gap was ~6 s, not 1 s: `logf()` only runs every
2 s and needs >1 s of drops before kicking.) The kick is now permanent in `logf()`.

Why it stalls in the first place (some race in the interrupt routine) is still unknown — the kick
makes that not matter.

## Side trouble: upload stuck after "Stub running"
While the silent firmware was running, `pio run -t upload` failed three times with
`A fatal error occurred: No serial data received.` right after `Stub running...` — even with no
other program holding the port. The ROM loader worked:
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --no-stub write_flash 0x10000 .pio/build/t-rgb/firmware.bin
```
(app only at 0x10000; 11 s). After that, normal uploads worked again. Cause not found.

Two self-inflicted failures on the way, both already in the skill: a background serial logger still
holding the port blocks uploads (G66), and `pkill -f "follow_serial..."` killed my own shell because
the pattern was in its command line — find the PID and `kill` that instead.

## C++ for Python people
- `bool touched(int &tx, int &ty)` — `&` makes the parameters *references*: the function writes into
  the caller's variables. Python would `return True, x, y`; C++ returns one value, so extra results
  come out through references.
- `constexpr int GAZE = +1;` — a compile-time constant. The compiler folds it into the arithmetic,
  so flipping it costs nothing at run time.
- `static int fingerX = -1;` inside `loop()` — a local variable that keeps its value between calls
  (like a module-level global, but only visible inside the function).
- `millis() | 1` — forces the timestamp odd, so it can never be 0, which means "not dropping".
