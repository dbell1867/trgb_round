// LilyGO T-RGB pin map — from the vendor's src/utilities.h + LilyGo_RGBPanel.h
// (github.com/Xinyuan-LilyGO/T-RGB, commit 359c03f).
//
// `constexpr` is a compile-time constant: like a Python module-level
// CONSTANT, but the compiler enforces it and it costs no RAM.
#pragma once
#include <stdint.h>

namespace pins {

// ---- ESP32-S3 GPIOs -------------------------------------------------------
constexpr int I2C_SDA   = 8;    // shared bus: XL9555, touch, Grove port
constexpr int I2C_SCL   = 48;
constexpr int TOUCH_IRQ = 1;
constexpr int TFT_BL    = 46;   // backlight (PWM-able)
constexpr int BATT_ADC  = 4;    // battery voltage / 2 (resistor divider)
constexpr int BOOT_BTN  = 0;    // BOOT button, active LOW

// RGB parallel panel timing signals
constexpr int TFT_HSYNC = 47;
constexpr int TFT_VSYNC = 41;
constexpr int TFT_DE    = 45;
constexpr int TFT_PCLK  = 42;

// microSD, 1-bit SD_MMC
constexpr int SD_CLK = 39;
constexpr int SD_CMD = 40;
constexpr int SD_D0  = 38;

}  // namespace pins

// ---- XL9555 I/O expander pins (NOT ESP32 GPIOs!) --------------------------
// Reached over I2C. utilities.h calls these TFT_CS=3, TFT_RST=6 ... which
// collide with RGB data GPIOs 3 and 6 — they are expander pin numbers.
namespace xl9555 {
constexpr uint8_t I2C_ADDR     = 0x20;
constexpr uint8_t TOUCH_RST    = 1;
constexpr uint8_t POWER_EN     = 2;  // must be HIGH (required on battery)
constexpr uint8_t LCD_CS       = 3;  // ST7701S 3-wire SPI, bit-banged via I2C
constexpr uint8_t LCD_MOSI     = 4;
constexpr uint8_t LCD_SCLK     = 5;
constexpr uint8_t LCD_RST      = 6;
constexpr uint8_t SD_EN        = 7;
}  // namespace xl9555
