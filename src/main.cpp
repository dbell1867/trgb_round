// Stage 1 — "Hello, serial": prove build -> flash -> serial works
// before touching the display.
#include <Arduino.h>      // like `import` — pulls in Serial, delay(), millis()...
#include "board_pins.h"   // quotes = our project's include/ folder

uint32_t counter = 0;     // a global, like a module-level variable in Python

void setup() {            // runs once after reset (CircuitPython: top of code.py)
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // G11: never block if no one is listening
    delay(1500);                // give the PC a moment to reopen the USB port

    Serial.println();
    Serial.println("=== T-RGB stage 1: hello, serial ===");
    Serial.printf("Chip: %s rev %d, %d cores @ %lu MHz\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
    Serial.printf("Flash: %lu MB\n", (unsigned long)(ESP.getFlashChipSize() >> 20));
    Serial.printf("PSRAM: %lu bytes total, %lu free\n",
                  (unsigned long)ESP.getPsramSize(), (unsigned long)ESP.getFreePsram());
    Serial.printf("Heap (internal RAM): %lu bytes free\n", (unsigned long)ESP.getFreeHeap());

    pinMode(pins::BOOT_BTN, INPUT_PULLUP);  // `pins::` picks the name out of the namespace
}

void loop() {             // runs forever (CircuitPython: `while True:`)
    Serial.printf("count=%lu  BOOT=%s\n", (unsigned long)counter++,
                  digitalRead(pins::BOOT_BTN) == LOW ? "pressed" : "up");
    delay(1000);
}
