// Stage 2b — text: Arduino_GFX draws into an off-screen canvas in PSRAM;
// the vendor library's pushColors() copies it to the panel.
#include <Arduino.h>
#include <LilyGo_RGBPanel.h>
#include <Arduino_GFX_Library.h>   // defines RGB565_* colours (as macros)
#include "board_pins.h"

constexpr int W = 480, H = 480;
constexpr int CX = W / 2, CY = H / 2;

LilyGo_RGBPanel panel;
// Canvas with no output device (nullptr): a pure in-memory drawing surface.
// `new` returns a pointer, so we use `->` (Python would just use `.`).
Arduino_Canvas *canvas = new Arduino_Canvas(W, H, nullptr);
uint16_t *fb = nullptr;            // the canvas's pixel buffer

// Uptime text lives in this full-width strip of rows.
constexpr int STRIP_Y = 296, STRIP_H = 32;

void drawCentred(const char *s, int y, uint8_t size, uint16_t colour) {
    int16_t x1, y1;
    uint16_t w, h;
    canvas->setTextSize(size);                         // built-in 6x8 font, scaled
    canvas->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);  // `&w` = "write the answer into w"
    canvas->setTextColor(colour);
    canvas->setCursor(CX - w / 2, y);
    canvas->print(s);
}

void pushStrip(int y, int h) {
    // Full-width rows are contiguous in memory, so a strip is one block.
    // TRAP: pushColors' last two args are named width/height in the header,
    // but are passed straight to esp_lcd_panel_draw_bitmap() as x_end/y_end
    // (exclusive). Passing a height crashes: "start position must be smaller".
    panel.pushColors(0, y, W, y + h, fb + y * W);
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // G11
    delay(1500);
    Serial.println("\n=== T-RGB stage 2b: text ===");

    panel.begin();
    Serial.printf("panel ok: model=%d touch=%s\n", (int)panel.getModel(), panel.getTouchModelName());

    bool ok = canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
    fb = canvas->getFramebuffer();
    Serial.printf("canvas->begin() -> %s, framebuffer at %p\n", ok ? "true" : "false", fb);
    if (!ok) return;

    canvas->fillScreen(RGB565_BLACK);
    canvas->drawCircle(CX, CY, 239, RGB565_RED);       // where is the visible edge?
    canvas->drawCircle(CX, CY, 232, RGB565_GREEN);
    canvas->drawCircle(CX, CY, 225, RGB565_BLUE);

    drawCentred("Hello, T-RGB!", 150, 4, RGB565_WHITE);
    drawCentred("ESP32-S3  8 MB PSRAM", 210, 2, RGB565_CYAN);
    drawCentred("480x480 ST7701S  GT911", 240, 2, RGB565_CYAN);

    uint32_t t0 = micros();
    panel.pushColors(0, 0, W, H, fb);
    Serial.printf("full frame push: %lu us\n", (unsigned long)(micros() - t0));

    panel.setBrightness(16);    // light it only after drawing (G21)
}

void loop() {
    static uint32_t reports = 0;   // `static` local: keeps its value between calls
    char buf[32];
    snprintf(buf, sizeof buf, "uptime %lu s", (unsigned long)(millis() / 1000));

    // Clear only the text area — the rings cross the strip near its ends.
    canvas->fillRect(90, STRIP_Y, W - 180, STRIP_H, RGB565_BLACK);
    drawCentred(buf, STRIP_Y + 4, 3, RGB565_YELLOW);

    uint32_t t0 = micros();
    pushStrip(STRIP_Y, STRIP_H);
    uint32_t dt = micros() - t0;
    if (reports++ < 3) Serial.printf("strip push (%d rows): %lu us\n", STRIP_H, (unsigned long)dt);

    delay(1000);
}
