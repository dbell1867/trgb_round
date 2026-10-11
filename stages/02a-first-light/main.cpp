// Stage 2a — first light: vendor library brings the panel up, we push
// one frame of colour bars with plain pushColors() (no graphics library).
#include <Arduino.h>
#include <Wire.h>
#include <LilyGo_RGBPanel.h>   // the vendor driver (from lib_deps)
#include "board_pins.h"

LilyGo_RGBPanel panel;         // a plain object (not a pointer) -> use `.` like Python

constexpr int W = 480, H = 480;

// RGB565: 16 bits per pixel = 5 bits red | 6 bits green | 5 bits blue.
constexpr uint16_t RED    = 0xF800;   // 11111 000000 00000
constexpr uint16_t GREEN  = 0x07E0;   // 00000 111111 00000
constexpr uint16_t BLUE   = 0x001F;   // 00000 000000 11111
constexpr uint16_t WHITE  = 0xFFFF;
constexpr uint16_t YELLOW = RED | GREEN;

void scanI2C() {
    Serial.print("I2C devices:");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", addr);
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // G11
    delay(1500);
    Serial.println("\n=== T-RGB stage 2a: first light ===");

    uint32_t t0 = millis();
    bool ok = panel.begin();    // power on, expander, touch detect, ST7701S init
    Serial.printf("panel.begin() -> %s in %lu ms\n", ok ? "true" : "false",
                  (unsigned long)(millis() - t0));
    Serial.printf("model=%d (3 = 2.8 inch)  touch=%s\n",
                  (int)panel.getModel(), panel.getTouchModelName());
    scanI2C();                  // the library already started Wire on SDA 8 / SCL 48

    // One full frame in PSRAM: 480*480*2 = 460,800 bytes — more than all
    // internal RAM, so it must come from ps_malloc (PSRAM), not malloc.
    uint16_t *frame = (uint16_t *)ps_malloc(W * H * sizeof(uint16_t));
    if (!frame) { Serial.println("ps_malloc failed!"); return; }
    Serial.printf("frame buffer at %p\n", frame);

    const uint16_t bars[4] = {RED, GREEN, BLUE, WHITE};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            frame[y * W + x] = bars[x / (W / 4)];      // 120-px-wide bars

    for (int y = 40; y < 80; y++)                       // yellow marker, top centre
        for (int x = 220; x < 260; x++)
            frame[y * W + x] = YELLOW;

    t0 = micros();
    panel.pushColors(0, 0, W, H, frame);
    Serial.printf("pushColors full frame: %lu us\n", (unsigned long)(micros() - t0));

    panel.setBrightness(16);    // 0..16 (pulse-count dimming); light it only after drawing (G21)
    Serial.println("backlight on");
}

void loop() {
    delay(1000);
}
