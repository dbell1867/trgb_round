// Stage 3 — touch: GT911 through the vendor library's getPoint().
// Tap the five crosshair targets to check orientation (G8), then paint.
// BOOT clears the screen. One summary line per stroke over serial (G9).
#include <Arduino.h>
#include <LilyGo_RGBPanel.h>
#include <Arduino_GFX_Library.h>
#include "board_pins.h"

constexpr int W = 480, H = 480;
constexpr int CX = W / 2, CY = H / 2;

LilyGo_RGBPanel panel;
Arduino_Canvas *canvas = new Arduino_Canvas(W, H, nullptr);
uint16_t *fb = nullptr;

// SensorLib's GT911 driver copies into a fixed 5-entry array, and its
// updateXY() writes as many entries as fingers detected — so always hand it
// 5-element arrays, whatever we actually use.
constexpr int MAX_POINTS = 5;
constexpr uint32_t STROKE_GAP_MS = 60;   // no touch frame for this long = finger lifted
constexpr int DOT_R = 4;
constexpr int INFO_Y = 96, INFO_H = 24;  // live coordinate readout strip

const uint16_t fingerColour[MAX_POINTS] = {
    RGB565_WHITE, RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_MAGENTA};

// A struct groups named fields — like a Python @dataclass with no methods.
struct Target { int x, y; const char *name; };
const Target targets[] = {
    {CX, 40, "T"}, {440, CY, "R"}, {CX, 440, "B"}, {40, CY, "L"}, {CX, CY, "C"}};

void pushRows(int y0, int y1) {          // push rows [y0, y1), clamped to the screen
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (y0 >= y1) return;
    panel.pushColors(0, y0, W, y1, fb + y0 * W);   // END coords, not w/h (stage 2b)
}

void drawCentred(const char *s, int y, uint8_t size, uint16_t colour) {
    int16_t x1, y1;
    uint16_t w, h;
    canvas->setTextSize(size);
    canvas->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    canvas->setTextColor(colour);
    canvas->setCursor(CX - w / 2, y);
    canvas->print(s);
}

void drawScene() {
    canvas->fillScreen(RGB565_BLACK);
    for (const Target &t : targets) {    // range-for: like `for t in targets:`
        canvas->drawFastHLine(t.x - 12, t.y, 25, RGB565_YELLOW);
        canvas->drawFastVLine(t.x, t.y - 12, 25, RGB565_YELLOW);
        canvas->setTextSize(2);
        canvas->setTextColor(RGB565_YELLOW);
        canvas->setCursor(t.x + 6, t.y + 6);
        canvas->print(t.name);
    }
    drawCentred("tap targets, then paint", 300, 2, RGB565_DARKGREY);
    drawCentred("BOOT clears", 324, 2, RGB565_DARKGREY);
    pushRows(0, H);
}

// A brush stroke as wide as the dot: stamp circles every ~2 px along the
// segment. (drawLine is 1 px wide — it looked like gaps between 9 px dots.)
void drawThickLine(int x0, int y0, int x1, int y1, int r, uint16_t colour) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = max(abs(dx), abs(dy)) / 2 + 1;
    for (int i = 0; i <= steps; i++)
        canvas->fillCircle(x0 + dx * i / steps, y0 + dy * i / steps, r, colour);
}

void showInfo(uint8_t n, int16_t x, int16_t y) {
    char buf[40];
    snprintf(buf, sizeof buf, "n=%u  x=%d  y=%d", n, x, y);
    canvas->fillRect(100, INFO_Y, W - 200, INFO_H, RGB565_BLACK);
    drawCentred(buf, INFO_Y + 4, 2, RGB565_CYAN);
    pushRows(INFO_Y, INFO_Y + INFO_H);
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // G11
    delay(1500);
    Serial.println("\n=== T-RGB stage 3: touch ===");

    panel.begin();
    Serial.printf("panel ok: model=%d touch=%s\n", (int)panel.getModel(), panel.getTouchModelName());
    if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) { Serial.println("canvas alloc failed"); return; }
    fb = canvas->getFramebuffer();

    pinMode(pins::BOOT_BTN, INPUT_PULLUP);
    drawScene();
    panel.setBrightness(16);    // after drawing (G21)
    Serial.println("targets: T(240,40) R(440,240) B(240,440) L(40,240) C(240,240)");
}

void loop() {
    // `static` locals keep their values between loop() calls (like attributes on self).
    static bool inStroke = false;
    static int16_t lastX, lastY, startX, startY;
    static uint32_t strokeStartMs, lastFrameMs;
    static uint32_t frames, empties, pendingEmpties;
    static uint8_t maxFingers;

    int16_t xs[MAX_POINTS], ys[MAX_POINTS];
    uint8_t n = panel.getPoint(xs, ys, MAX_POINTS);
    uint32_t now = millis();

    if (n == 0) {
        if (inStroke) {
            pendingEmpties++;            // only counted if another frame follows
            if (now - lastFrameMs > STROKE_GAP_MS) {
                uint32_t dur = lastFrameMs - strokeStartMs;
                float hz = (frames > 1 && dur > 0) ? (frames - 1) * 1000.0f / dur : 0;
                Serial.printf("stroke: start=(%d,%d) end=(%d,%d) frames=%lu empty_polls=%lu "
                              "dur=%lums rate=%.0fHz max_fingers=%u\n",
                              startX, startY, lastX, lastY, (unsigned long)frames,
                              (unsigned long)empties, (unsigned long)dur, hz, maxFingers);
                inStroke = false;
            }
        }
    } else {
        if (n > MAX_POINTS) n = MAX_POINTS;
        int16_t x = xs[0], y = ys[0];
        if (!inStroke) {                 // finger down: new stroke
            inStroke = true;
            startX = lastX = x;
            startY = lastY = y;
            strokeStartMs = now;
            frames = empties = pendingEmpties = 0;
            maxFingers = 0;
        }
        frames++;
        empties += pendingEmpties;
        pendingEmpties = 0;
        if (n > maxFingers) maxFingers = n;
        lastFrameMs = now;

        // Finger 0: thick segment from the previous sample (continuous at any speed, G12).
        drawThickLine(lastX, lastY, x, y, DOT_R, fingerColour[0]);
        int y0 = min(lastY, y) - DOT_R, y1 = max(lastY, y) + DOT_R + 1;
        pushRows(y0, y1);
        lastX = x;
        lastY = y;

        for (int i = 1; i < n; i++) {    // extra fingers: dots only
            canvas->fillCircle(xs[i], ys[i], DOT_R, fingerColour[i]);
            pushRows(ys[i] - DOT_R, ys[i] + DOT_R + 1);
        }
        if (frames % 4 == 1) showInfo(n, x, y);   // readout ~25x/s is plenty
    }

    if (digitalRead(pins::BOOT_BTN) == LOW) {
        drawScene();
        Serial.println("cleared");
        while (digitalRead(pins::BOOT_BTN) == LOW) delay(10);   // wait for release
    }
    delay(1);
}
