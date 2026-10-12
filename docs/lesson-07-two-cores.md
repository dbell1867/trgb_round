# Lesson 07 — Two cores, one memory bus (a negative result)

**Goal:** smoother motion by rendering on one core while the other pushes the previous frame.

## The pipeline
Two 480×480 frame buffers in PSRAM and two FreeRTOS queues (like two Python `queue.Queue`s
shared by two threads):
- `freeQ` — buffers ready to draw into (pusher → renderer)
- `readyQ` — finished frames (renderer → pusher)

`loop()` (core 1) takes a free buffer, renders, puts it on `readyQ`; `pushTask` (pinned to core 0)
takes it, `pushColors`, gives it back. Neither core can hold a buffer the other is using, so the
queues are the only synchronisation. The pusher's statistics only ever increase and are only written
by the pusher; `loop()` reads them and takes differences, so the cores never write the same variable.

## Result
| | Alone (stage 6) | Pipelined | |
|---|---|---|---|
| render (core 1) | 27 ms | 42–45 ms | waits ~4–5 ms for a free buffer |
| push (core 0) | 24 ms | 47–49 ms | waits ~0 → the bottleneck |
| frame rate | 19.6 fps | **20.3–21.2 fps** | predicted 24–30 ✗ |

Both jobs got ~2× slower when run together. Separate cores only slow each other through something
they **share**: here the memory bus. On the ESP32-S3 the octal PSRAM *and* the flash (eye tables) sit
on the same MSPI bus, and the LCD's DMA streams the frame out of PSRAM continuously. Rough budget at
20 fps: push ~37 MB/s (read + write 0.9 MB/frame) + render ~28 MB/s + scanout ~25 MB/s (0.9 MB × 27 Hz)
≈ 90 MB/s — about what this PSRAM delivers in practice. **The limit is bytes moved, not CPU.**

A follow-up "trim" (write both doubled rows directly instead of `memcpy`-ing the row below) changed
nothing — but the test was flawed: the row being copied had just been written and was still in the
cache, so the copy never touched PSRAM. Kept anyway (equivalent, simpler).

Decision: keep the pipeline (slightly faster, harmless), stop here for now.

## Bug: reset every ~10 s (task watchdog)
After the commit the user saw "the animation keeps resetting". Serial without a reset:
```
task_wdt: Task watchdog got triggered ... did not reset the watchdog in time:
task_wdt:  - IDLE0 (CPU 0)
task_wdt: CPU 0: push
```
The task watchdog reboots the chip if core 0's idle task gets no CPU time for 5 s. `pushTask` ran at
priority 2 (above idle) and, being the bottleneck, *always* found a frame waiting — it never blocked,
so IDLE0 never ran. **My checks were 14 s long; the first reset came ~18 s after boot** — the same
trap as lesson 05: testing only in a window where the bug can't appear.

| Fix attempt | Result |
|---|---|
| push at idle priority (0) | no resets, but push 48 → 63–68 ms, 15 fps ✗ — each time idle gets its turn it sleeps (`waiti`) until the next 1 ms tick, so push lost ~every other tick |
| priority 2 + `vTaskDelay(1)` after each push | ✓ ~19.7 fps, **0 watchdog events / 0 reboots over 121 s** |

Rule: a task that may never block must give the idle task time explicitly — and soak-test longer
than the watchdog timeout (5 s here; minutes is better).

## The idea for next time: "race the beam"
The push is a full 0.9 MB PSRAM→PSRAM copy every frame — ~40 % of the bus traffic. Remove it:
1. **Render straight into the driver's frame buffer.** IDF 4.4 has no getter; find it by scanning the
   panel object for the frame-buffer size (460,800) next to a PSRAM pointer, and *prove* it by
   reading back a pattern pushed with `pushColors`.
2. **No tearing by timing:** the panel scans top→bottom in ~37 ms, rendering takes ~27 ms. Start
   rendering at VSYNC (GPIO 41, readable with the input-enable trick from lesson 05) and the renderer
   stays ahead of the scan all the way down → every scan shows a complete frame.
3. **Write back the cache** (`Cache_WriteBack_Addr`) per band of rows, since the DMA reads PSRAM, not
   the cache.
Expected ~27 fps locked to refresh, one core, one buffer. The official alternative is Arduino core 3 /
IDF 5 (RGB panel with two frame buffers and a tear-free swap) — a bigger migration.

## C++ / FreeRTOS for Python people
- `xQueueCreate(2, sizeof(uint16_t *))` — a queue of 2 items, each a *copy* of a pointer (FreeRTOS
  queues copy bytes in and out, so you pass `&buf`).
- `xQueueReceive(q, &buf, portMAX_DELAY)` ≈ `buf = q.get()` (block forever).
- `xTaskCreatePinnedToCore(fn, "push", 4096, nullptr, 2, nullptr, 0)` ≈ `threading.Thread(target=fn)`,
  plus a stack size (bytes), a priority, and which core it may run on. Arduino's `loop()` runs on core 1.
- `volatile` tells the compiler a variable may change behind its back (another core), so it must
  re-read it from memory each time instead of keeping it in a register.
