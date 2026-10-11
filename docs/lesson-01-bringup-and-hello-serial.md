# Lesson 01 — Bring-up, factory backup, "Hello, serial"

**Board:** LilyGO T-RGB 2.8" full circle · **Date:** 2026-10-11 · **Code:** `stages/01-hello-serial/`

## Objectives
1. Identify the board from the hardware itself, not the box.
2. Research its pins/chips from the vendor source.
3. Back up the factory firmware, verified by the chip.
4. Prove the build → flash → serial pipeline with a trivial program.

## 1. Identify
| Probe | Result | Meaning |
|---|---|---|
| `lsusb` | `303a:1001 Espressif USB JTAG/serial debug unit` | native-USB ESP (S3/C3/C6), no USB-serial chip |
| `ls -l /dev/ttyACM0` | group `uucp` | user already in `uucp` → no permission work |
| `esptool flash-id` | ESP32-S3 (QFN56) rev v0.1, Embedded PSRAM 8MB, flash `ef/4018` 16 MB | S3**R8** = octal PSRAM → `qio_opi`; Winbond W25Q128 |

## 2. Research (github.com/Xinyuan-LilyGO/T-RGB @ 359c03f)
- Display: ST7701S 480×480, **RGB parallel** (16 data lines + HSYNC/VSYNC/DE/PCLK), backlight GPIO 46.
- **XL9555 I²C I/O expander** (0x20) on SDA 8 / SCL 48 owns touch reset, **power enable**, LCD CS/MOSI/SCLK/reset, SD enable.
- Touch by variant: CST820 (2.1" full) / FT3267 (2.1" half) / **GT911 (2.8")** — ours, not yet confirmed.
- No free GPIOs; Grove port = the shared I²C bus. Battery on ADC GPIO 4 (×2 divider), no PMIC.
- Buttons (schematic): **S1 = BOOT → GPIO0** (pulled up), **SW1 = RST → CHIP_PU**.

**Lesson: read the source, not just the header.** `utilities.h` says `TFT_CS 3`, `TFT_RST 6` —
but GPIO 3 and 6 are RGB data lines. The driver (`LilyGo_RGBPanel.h`) shows they are
*expander* pin numbers. A header alone would have sent us driving the wrong pins.

## 3. Factory backup — a deterministic failure (G42)
- `esptool read-flash 0 ALL` failed: `Packet content transfer stopped` at 0x1b2000.
- **Prediction:** deterministic, content-dependent → the 1 MB chunk at 0x100000 fails again. **Held.**
- Fix = change the *method*, not the retry count: `scripts/backup_flash.py`
  (1 MB stub reads → 64 KB → 4 KB ROM reads with `--no-stub`). Bad pieces: 0x1b0000, 0x210000
  (both inside `app0`); ROM reader read all 32 blocks. ~3 min.
- `verify-flash` → **"digest matched"**. File read-only + SHA-256. Details & restore: `backup/README.md`.

## 4. Approach decision
**A: LilyGO's library at their pinned versions** (espressif32@6.9.0 = Arduino core 2.0.17, LVGL 8).
Panel init is ~40 commands bit-banged through the expander, with per-variant tables — a wrong
guess gives a silent black screen. We learn by *reading* their driver. Option B (Arduino_GFX,
core 3.x) stays open for later.

## 5. Stage 1 — Hello, serial
Files: `platformio.ini`, `boards/LilyGo-T-RGB.json` (vendor), `include/board_pins.h`, `src/main.cpp`.
No libraries — so any failure is the toolchain.

| Prediction | Measured | |
|---|---|---|
| ESP32-S3, 2 cores @ 240 MHz | `ESP32-S3 rev 0, 2 cores @ 240 MHz` | ✅ |
| Flash 16 MB | 16 MB | ✅ |
| PSRAM slightly under 8 MiB | 8 386 295 B (≈2.3 KB reserved) | ✅ |
| Screen goes black (nothing drives backlight/panel) | black | ✅ |
| BOOT idle HIGH, pressed LOW | `up` idle; `pressed` ×5 lines while held | ✅ |

Internal heap free: 370 716 B. **One 480×480 RGB565 frame = 460 800 B > all internal RAM**, so
frame buffers must live in PSRAM.

Build: RAM 5.7 %, flash 4.0 % of the 6.25 MB app slot. Upload ~5 s, every region hash-verified.

### Boot banner decoded
- `rst:0x15 (USB_UART_CHIP_RESET)` — reset caused by the reader toggling USB DTR/RTS.
- `mode:DIO` — the ROM loads the 2nd-stage bootloader in safe 2-wire mode; harmless (G55 would boot-loop).

## Real problems hit
1. **Flash read stalls** — above (G42).
2. **Testing a button with a resetting reader is a trap.** `read_serial.py` resets the board
   via DTR; holding BOOT during a reset enters **download mode** instead of our program. Use
   `scripts/follow_serial.py` (never resets) for anything involving BOOT.
3. **Human + tool timing.** First button run missed: the press (and an RST) fell outside the
   read window. Count kept climbing (no reset) — that's how we knew RST wasn't pressed *during* it.

## C++ for Python people
| C++ | Python analogue |
|---|---|
| `#include <Arduino.h>` | `import` (angle brackets = system/library; quotes = this project) |
| `constexpr int TFT_BL = 46;` | `TFT_BL = 46` but enforced constant, zero RAM |
| `namespace pins { ... }` / `pins::TFT_BL` | a module / `pins.TFT_BL` |
| `setup()` / `loop()` | top of `code.py` / `while True:` |
| `Serial.printf("%lu", (unsigned long)x)` | f-string; the cast matches the `%lu` format exactly |
| `cond ? "a" : "b"` | `"a" if cond else "b"` |

## Cheat-sheet
```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run                                   # build
pio run -t upload                         # flash
~/.platformio/penv/bin/python scripts/read_serial.py /dev/ttyACM0 8     # read (resets board)
~/.platformio/penv/bin/python scripts/follow_serial.py /dev/ttyACM0 30  # read (no reset)
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 backup/factory.bin   # restore factory
```
Manual download mode: hold BOOT, tap RST, release BOOT.

## Glossary
- **RGB parallel panel** — pixels streamed continuously over many data lines with sync signals, like video; the ESP's LCD peripheral reads the frame from memory via DMA.
- **I/O expander** — a chip that adds GPIOs, controlled over I²C. Slower than real GPIOs.
- **Strapping pin** — a GPIO the ROM samples at reset to pick boot mode (GPIO0, GPIO46 on the S3).
- **Stub / ROM reader** — esptool's uploaded fast helper vs. the chip's built-in bootloader protocol.
- **PSRAM (octal, OPI)** — external RAM in the S3R8 package on an 8-bit bus; big but slower than internal SRAM.
