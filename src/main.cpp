// Stage 3b — interrupt-driven touch. An ISR on the GT911 INT line (GPIO 1)
// counts every edge; loop() samples the INT level. BOOT cycles three modes:
//   POLL   — read every loop (stage 3a behaviour)
//   LEVEL  — read only while INT is LOW (= finger on screen), plus one read after
//   NOREAD — never read/clear; just watch INT
// (Measured: on this board INT does NOT pulse per report — it is LOW while touched.)
// One stats line per second over serial; painting works in POLL and IRQ.
#include <Arduino.h>
#include <Wire.h>
#include <LilyGo_RGBPanel.h>
#include <Arduino_GFX_Library.h>
#include "board_pins.h"

constexpr int W = 480, H = 480;
constexpr int CX = W / 2, CY = H / 2;

LilyGo_RGBPanel panel;
Arduino_Canvas *canvas = new Arduino_Canvas(W, H, nullptr);
uint16_t *fb = nullptr;

constexpr int MAX_POINTS = 5;            // SensorLib GT911 internals: always 5 (stage 3a)
constexpr uint32_t STROKE_GAP_MS = 60;
constexpr int DOT_R = 4;
constexpr int INFO_Y = 96, INFO_H = 24;  // live coordinate readout strip
constexpr int MODE_Y = 150, MODE_H = 24; // mode label strip
constexpr uint8_t GT911_ADDR = 0x5D;

enum class Mode { POLL, LEVEL, NOREAD };   // `enum class`: named constants, like Python's Enum
Mode mode = Mode::LEVEL;                 // measured winner (stage 3b)
const char *modeName(Mode m) {
    switch (m) {
        case Mode::POLL: return "POLL";
        case Mode::LEVEL: return "LEVEL";
        default:         return "NOREAD";
    }
}

const uint16_t fingerColour[MAX_POINTS] = {
    RGB565_WHITE, RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_MAGENTA};

struct Target { int x, y; const char *name; };
const Target targets[] = {
    {CX, 40, "T"}, {440, CY, "R"}, {CX, 440, "B"}, {40, CY, "L"}, {CX, CY, "C"}};

// ---- Interrupt side -------------------------------------------------------
// `volatile`: the ISR changes these behind loop()'s back, so the compiler must
// re-read them every time instead of caching them in a register.
volatile uint32_t isrChanges = 0, isrFalls = 0;
volatile uint32_t isrLastFallUs = 0, isrLowMinUs = UINT32_MAX, isrLowMaxUs = 0;
volatile uint32_t isrLastEdgeUs = 0, isrGapMinUs = UINT32_MAX;  // needs no pin read
volatile bool frameReady = false;
// A spinlock so loop() can copy-and-reset the counters without the ISR
// changing them halfway (the ESP32 way to protect data shared with an ISR).
portMUX_TYPE isrMux = portMUX_INITIALIZER_UNLOCKED;

// IRAM_ATTR: keep the ISR in internal RAM so it runs even while flash is busy.
void IRAM_ATTR onTouchInt() {
    uint32_t now = micros();
    portENTER_CRITICAL_ISR(&isrMux);
    isrChanges++;
    if (isrLastEdgeUs && now - isrLastEdgeUs < isrGapMinUs) isrGapMinUs = now - isrLastEdgeUs;
    isrLastEdgeUs = now;
    if (digitalRead(pins::TOUCH_IRQ) == LOW) {          // falling edge: report ready
        isrFalls++;
        isrLastFallUs = now;
        frameReady = true;
    } else if (isrLastFallUs) {                          // rising edge: end of LOW pulse
        uint32_t w = now - isrLastFallUs;
        if (w < isrLowMinUs) isrLowMinUs = w;
        if (w > isrLowMaxUs) isrLowMaxUs = w;
    }
    portEXIT_CRITICAL_ISR(&isrMux);
}

// ---- Drawing helpers --------------------------------------------------------
void pushRows(int y0, int y1) {          // push rows [y0, y1), clamped
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

void drawThickLine(int x0, int y0, int x1, int y1, int r, uint16_t colour) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = max(abs(dx), abs(dy)) / 2 + 1;
    for (int i = 0; i <= steps; i++)
        canvas->fillCircle(x0 + dx * i / steps, y0 + dy * i / steps, r, colour);
}

void drawScene() {
    canvas->fillScreen(RGB565_BLACK);
    for (const Target &t : targets) {
        canvas->drawFastHLine(t.x - 12, t.y, 25, RGB565_YELLOW);
        canvas->drawFastVLine(t.x, t.y - 12, 25, RGB565_YELLOW);
        canvas->setTextSize(2);
        canvas->setTextColor(RGB565_YELLOW);
        canvas->setCursor(t.x + 6, t.y + 6);
        canvas->print(t.name);
    }
    char buf[32];
    snprintf(buf, sizeof buf, "mode: %s", modeName(mode));
    drawCentred(buf, MODE_Y + 4, 2, RGB565_ORANGE);
    drawCentred("BOOT = next mode", 324, 2, RGB565_DARKGREY);
    pushRows(0, H);
}

void showInfo(uint8_t n, int16_t x, int16_t y) {
    char buf[40];
    snprintf(buf, sizeof buf, "n=%u  x=%d  y=%d", n, x, y);
    canvas->fillRect(100, INFO_Y, W - 200, INFO_H, RGB565_BLACK);
    drawCentred(buf, INFO_Y + 4, 2, RGB565_CYAN);
    pushRows(INFO_Y, INFO_Y + INFO_H);
}

// Raw GT911 register read (16-bit register address) — our own probe,
// independent of SensorLib's getResolution().
bool gt911Read(uint16_t reg, uint8_t *buf, uint8_t n) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(GT911_ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

void probeResolution() {
    uint8_t cfg[4], info[4];
    if (gt911Read(0x8048, cfg, 4) && gt911Read(0x8146, info, 4)) {
        Serial.printf("GT911 config 0x8048 output max: X=%u Y=%u | info 0x8146 resolution: X=%u Y=%u\n",
                      cfg[0] | cfg[1] << 8, cfg[2] | cfg[3] << 8,
                      info[0] | info[1] << 8, info[2] | info[3] << 8);
    } else {
        Serial.println("GT911 raw register read failed");
    }
}

// ---- Main -----------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // G11
    delay(1500);
    Serial.println("\n=== T-RGB stage 3b: interrupt-driven touch ===");

    panel.begin();
    Serial.printf("panel ok: model=%d touch=%s\n", (int)panel.getModel(), panel.getTouchModelName());
    if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) { Serial.println("canvas alloc failed"); return; }
    fb = canvas->getFramebuffer();

    probeResolution();

    pinMode(pins::BOOT_BTN, INPUT_PULLUP);
    // Probe: is INT driven at idle, or floating? Read it under each pull setting.
    const int pulls[] = {INPUT, INPUT_PULLUP, INPUT_PULLDOWN};
    const char *pullNames[] = {"no pull", "pull-up", "pull-down"};
    for (int i = 0; i < 3; i++) {
        pinMode(pins::TOUCH_IRQ, pulls[i]);
        delay(5);                                  // let the line settle
        int highs = 0;
        for (int k = 0; k < 100; k++) { highs += digitalRead(pins::TOUCH_IRQ); delayMicroseconds(100); }
        Serial.printf("INT idle with %-9s: %3d/100 samples HIGH\n", pullNames[i], highs);
    }
    pinMode(pins::TOUCH_IRQ, INPUT);               // back to what the library set
    attachInterrupt(pins::TOUCH_IRQ, onTouchInt, CHANGE);

    drawScene();
    panel.setBrightness(16);    // after drawing (G21)
}

void loop() {
    static bool inStroke = false;
    static int16_t lastX, lastY;
    static uint32_t lastFrameMs;
    static uint32_t reads, frames, empties;     // per-second counters (loop side)
    static uint32_t statsStartMs = millis();
    static uint32_t samples, lowSamples;        // INT level sampling
    static bool wasLow = false;

    uint32_t now = millis();

    // 1. Sample the INT level, then decide whether to read the touch controller.
    bool intLow = digitalRead(pins::TOUCH_IRQ) == LOW;
    samples++;
    lowSamples += intLow;
    bool doRead = false;
    if (mode == Mode::POLL) {
        doRead = true;
    } else if (mode == Mode::LEVEL) {
        doRead = intLow || wasLow;               // while LOW, plus one read after it rises
    }
    wasLow = intLow;

    // 2. Read and paint.
    if (doRead) {
        int16_t xs[MAX_POINTS], ys[MAX_POINTS];
        uint8_t n = panel.getPoint(xs, ys, MAX_POINTS);
        reads++;
        if (n == 0) {
            empties++;
        } else {
            if (n > MAX_POINTS) n = MAX_POINTS;
            frames++;
            int16_t x = xs[0], y = ys[0];
            if (!inStroke) { inStroke = true; lastX = x; lastY = y; }
            lastFrameMs = now;
            drawThickLine(lastX, lastY, x, y, DOT_R, fingerColour[0]);
            pushRows(min(lastY, y) - DOT_R, max(lastY, y) + DOT_R + 1);
            lastX = x;
            lastY = y;
            for (int i = 1; i < n; i++) {
                canvas->fillCircle(xs[i], ys[i], DOT_R, fingerColour[i]);
                pushRows(ys[i] - DOT_R, ys[i] + DOT_R + 1);
            }
            if (frames % 4 == 1) showInfo(n, x, y);
        }
    }
    // Stroke end is time-based, so it works even when we skip reads (IRQ mode).
    if (inStroke && now - lastFrameMs > STROKE_GAP_MS) inStroke = false;

    // 3. Once per second: snapshot ISR counters (under the lock) and report.
    if (now - statsStartMs >= 1000) {
        portENTER_CRITICAL(&isrMux);
        uint32_t changes = isrChanges, falls = isrFalls;
        uint32_t lowMin = isrLowMinUs, lowMax = isrLowMaxUs, gapMin = isrGapMinUs;
        isrGapMinUs = UINT32_MAX;
        isrChanges = isrFalls = 0;
        isrLowMinUs = UINT32_MAX;
        isrLowMaxUs = 0;
        portEXIT_CRITICAL(&isrMux);

        if (changes || frames || lowSamples) {   // stay quiet when nothing happens
            Serial.printf("[%s] int_low=%3lu%% int_changes=%lu min_gap_us=%lu | "
                          "reads=%lu frames=%lu empties=%lu\n",
                          modeName(mode), (unsigned long)(lowSamples * 100 / samples),
                          (unsigned long)changes, (unsigned long)(gapMin == UINT32_MAX ? 0 : gapMin),
                          (unsigned long)reads, (unsigned long)frames, (unsigned long)empties);
        }
        (void)falls; (void)lowMin; (void)lowMax;  // edge-level classification proved unreliable
        reads = frames = empties = 0;
        samples = lowSamples = 0;
        statsStartMs = now;
    }

    // 4. BOOT: next mode.
    if (digitalRead(pins::BOOT_BTN) == LOW) {
        mode = (mode == Mode::LEVEL) ? Mode::NOREAD : (mode == Mode::NOREAD) ? Mode::POLL : Mode::LEVEL;
        frameReady = false;
        inStroke = false;
        drawScene();
        Serial.printf("---- mode -> %s ----\n", modeName(mode));
        while (digitalRead(pins::BOOT_BTN) == LOW) delay(10);
    }
    delay(1);
}
