# trgb_round

Learning embedded C++ on a **LilyGO T-RGB 2.8"** — an ESP32-S3 board with a 480×480
round touch display — one proven stage at a time, from a bare board to custom firmware.
Every stage has predictions, measured results and a write-up in [`docs/`](docs/).

## Hardware

| | |
|---|---|
| **Board** | LilyGO T-RGB, 2.8" full-circle variant |
| **MCU** | ESP32-S3R8 — dual-core Xtensa LX7 @ 240 MHz, Wi-Fi, BLE 5 |
| **Memory** | 16 MB flash (Winbond W25Q128), 8 MB octal PSRAM |
| **Display** | 480×480 round IPS, ST7701S driver, RGB parallel interface |
| **Touch** | GT911 capacitive, I²C 0x5D (confirmed) |
| **I/O expander** | XL9555 on I²C — owns LCD reset/init lines, touch reset, power enable, SD enable |
| **Other** | microSD (1-bit SD_MMC), LiPo connector (voltage on GPIO 4), Grove I²C port, BOOT + RST buttons |

Board facts were researched from [Xinyuan-LilyGO/T-RGB](https://github.com/Xinyuan-LilyGO/T-RGB)
(commit `359c03f`) and checked against the chip itself. Full pin map:
[`include/board_pins.h`](include/board_pins.h).

> **Quirk worth knowing:** the vendor header lists `TFT_CS 3` and `TFT_RST 6`, but GPIO 3 and 6
> are RGB data lines. Those numbers are **XL9555 expander pins** — the panel's init commands are
> bit-banged over I²C. `board_pins.h` keeps ESP32 GPIOs (`pins::`) and expander pins (`xl9555::`)
> in separate namespaces so they can't be confused.

## Progress

| Stage | Topic | Status | Write-up |
|---|---|---|---|
| 0 | Detect, research, factory backup | ✅ done | [lesson 01](docs/lesson-01-bringup-and-hello-serial.md) |
| 1 | Hello, serial — prove build → flash → serial | ✅ done | [lesson 01](docs/lesson-01-bringup-and-hello-serial.md) |
| 2 | First light — vendor library, colour bars + text | ✅ done | [lesson 02](docs/lesson-02-first-light.md) |
| 3 | Touch input (GT911) — orientation, rate, finger painting | ✅ done | [lesson 03](docs/lesson-03-touch.md) |
| … | LVGL UI, microSD, Wi-Fi, battery + deep sleep | ideas | — |

Current position and next steps: [`docs/PLAN.md`](docs/PLAN.md).

## Approach

C++ with the Arduino framework on **PlatformIO**, built on LilyGO's own board library at the
versions they pin (`espressif32@6.9.0` = Arduino core 2.0.17, LVGL 8). The panel init sequence
is per-variant and goes through the expander, so the vendor driver is used for bring-up and
*read* to understand how it works. A lower-level Arduino_GFX / core 3.x port may follow later.

## Build and flash

Requires [PlatformIO Core](https://platformio.org/install/cli).

```bash
pio run                      # build (first run downloads the toolchain)
pio run -t upload            # flash over USB-C
python scripts/read_serial.py /dev/ttyACM0 8      # read serial (resets the board)
python scripts/follow_serial.py /dev/ttyACM0 30   # read serial without resetting
```

Run the scripts with PlatformIO's Python (`~/.platformio/penv/bin/python`) so `pyserial` is
available. If upload can't connect: hold **BOOT**, tap **RST**, release **BOOT**, retry.

## Repository layout

```
platformio.ini        build config (vendor platform + board)
boards/               LilyGO board definition (16 MB flash, OPI PSRAM, native USB)
include/board_pins.h  pin map as constexpr constants
src/main.cpp          the current stage's firmware
stages/NN-*/          frozen snapshot of each completed stage
docs/                 lesson write-ups and PLAN.md
scripts/              flash backup + serial helpers
backup/               verified factory firmware image + restore instructions
```

## Factory firmware

The original flash was dumped and verified against the chip before the first upload — see
[`backup/README.md`](backup/README.md). Restore with:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 backup/factory.bin
```

A plain `esptool read-flash 0 ALL` fails on this board (`Packet content transfer stopped` at
fixed addresses); [`scripts/backup_flash.py`](scripts/backup_flash.py) works around it by reading
in chunks and falling back to the ROM reader for the blocks that stall.
