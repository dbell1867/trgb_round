// Stage 4a — LVGL 8 through LilyGO's glue (beginLvglHelper): a round arc
// gauge, a value label, and a button that counts its own events (to test
// for press/release bounce). A refresh monitor reports redraw cost.
#include <Arduino.h>
#include <LilyGo_RGBPanel.h>
#include <LV_Helper.h>     // beginLvglHelper(): LVGL display + touch glue (pulls in lvgl.h)

LilyGo_RGBPanel panel;

lv_obj_t *valueLabel = nullptr;  // lv_obj_t* = a handle to an LVGL widget
lv_obj_t *countLabel = nullptr;

uint32_t nPressed, nReleased, nClicked, nLongPressed, nPressLost;

// Refresh monitor totals. monitor_cb runs inside lv_timer_handler(), i.e. in
// loop()'s context — no ISR involved, so no volatile/locks needed.
uint32_t refreshes, refrMsTotal, refrMsMax, refrPx;

void onMonitor(lv_disp_drv_t *drv, uint32_t ms, uint32_t px) {
    refreshes++;
    refrMsTotal += ms;
    if (ms > refrMsMax) refrMsMax = ms;
    refrPx += px;
}

void onArc(lv_event_t *e) {
    lv_obj_t *arc = lv_event_get_target(e);
    lv_label_set_text_fmt(valueLabel, "%d", (int)lv_arc_get_value(arc));
}

void updateCounts() {
    lv_label_set_text_fmt(countLabel, "pressed %lu  released %lu\nclicked %lu  long %lu  lost %lu",
                          (unsigned long)nPressed, (unsigned long)nReleased, (unsigned long)nClicked,
                          (unsigned long)nLongPressed, (unsigned long)nPressLost);
    Serial.printf("button: pressed=%lu released=%lu clicked=%lu long=%lu lost=%lu\n",
                  (unsigned long)nPressed, (unsigned long)nReleased, (unsigned long)nClicked,
                  (unsigned long)nLongPressed, (unsigned long)nPressLost);
}

// One callback for all of the button's events; the event code says which.
void onButton(lv_event_t *e) {
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:      nPressed++;     break;
        case LV_EVENT_RELEASED:     nReleased++;    break;
        case LV_EVENT_CLICKED:      nClicked++;     break;
        case LV_EVENT_LONG_PRESSED: nLongPressed++; break;
        case LV_EVENT_PRESS_LOST:   nPressLost++;   break;
        default: return;            // ignore the many other events (draw, focus...)
    }
    updateCounts();
}

void buildUi() {
    lv_obj_t *scr = lv_scr_act();                      // the current screen
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Arc gauge around the edge: 270° sweep starting bottom-left.
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 440, 440);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 40);
    lv_obj_set_style_arc_width(arc, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 24, LV_PART_INDICATOR);
    lv_obj_add_event_cb(arc, onArc, LV_EVENT_VALUE_CHANGED, nullptr);

    valueLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(valueLabel, lv_color_white(), 0);
    lv_label_set_text(valueLabel, "40");
    lv_obj_align(valueLabel, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *btn = lv_btn_create(scr);                // created after the arc = drawn on top
    lv_obj_set_size(btn, 180, 70);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(btn, onButton, LV_EVENT_ALL, nullptr);
    lv_obj_t *btnLabel = lv_label_create(btn);
    lv_label_set_text(btnLabel, "Press me");
    lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_24, 0);
    lv_obj_center(btnLabel);

    countLabel = lv_label_create(scr);
    lv_obj_set_style_text_color(countLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(countLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(countLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(countLabel, LV_ALIGN_CENTER, 0, 105);
    updateCounts();
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // G11
    delay(1500);
    Serial.println("\n=== T-RGB stage 4a: LVGL ===");

    panel.begin();
    Serial.printf("panel ok: model=%d touch=%s\n", (int)panel.getModel(), panel.getTouchModelName());

    beginLvglHelper(panel);     // lv_init + 2 PSRAM draw buffers + flush/touch callbacks
    // The glue's display driver is private, but LVGL gives us a pointer to it.
    lv_disp_get_default()->driver->monitor_cb = onMonitor;

    buildUi();
    uint32_t t0 = millis();
    lv_refr_now(nullptr);       // render the first frame now...
    Serial.printf("first full render: %lu ms\n", (unsigned long)(millis() - t0));
    panel.setBrightness(16);    // ...and only then light it (G21)
}

void loop() {
    static uint32_t lastReport = millis();
    lv_timer_handler();         // LVGL: read touch (every 30 ms), run timers, redraw dirty areas

    if (millis() - lastReport >= 1000) {
        if (refreshes) {
            Serial.printf("refresh: %lu/s  avg %lu ms  max %lu ms  %lu px redrawn\n",
                          (unsigned long)refreshes, (unsigned long)(refrMsTotal / refreshes),
                          (unsigned long)refrMsMax, (unsigned long)refrPx);
        }
        refreshes = refrMsTotal = refrMsMax = refrPx = 0;
        lastReport = millis();
    }
    delay(2);
}
