// display.h — LVGL UI driver for the Waveshare 1.64" AMOLED display.
//
// Drives the CO5300 AMOLED (280×456 pixels) via QSPI using a custom SH8601
// driver adapted from the Waveshare Arduino demo (docs/Waveshare Demo/).
// Do NOT use TFT_eSPI for this display — it does not support the CO5300.
//
// LVGL 8.3 renders the UI in a dedicated FreeRTOS task pinned to core 0.
// The main loop() runs on core 1. Synchronisation between the two is handled
// by a FreeRTOS mutex inside display.cpp — do not call lv_obj_* functions from
// main() directly without taking the mutex first.
//
// Touch input:
//   The FT3168 capacitive touch controller is polled over I²C in display_poll_touch(),
//   which MUST be called from the main task (core 1), not from the LVGL task.
//   This is because the Wire library is not thread-safe and the I²C bus is shared.
//   display_poll_touch() packages the touch coordinates and injects a pointer event
//   into LVGL, which processes it on the next render tick.
//
// Screen layout (swipeable tileview — swipe left/right to navigate):
//   Tile 0 — Status     mode (WiFi/ELRS), link quality, bridge connection, voltage, GPS
//   Tile 1 — Attitude   roll / pitch / yaw gauges
//   Tile 2 — GPS        fix status, lat/lng, speed, altitude, satellite count
//   Tile 3 — Power      voltage bar, current, mAh used; bilge and pump status
//   Tile 4 — Log        SD card status, log filename, row count
//   Tile 5 — Compass    live heading dial with cardinal labels (COMPASS_ENABLED only)

#pragma once

// Initialise CO5300 display, LVGL, FT3168 touch, and start the LVGL render task.
// Call once from setup() after Wire.begin(). Blocks briefly while the display panel resets.
void display_init();

// Refresh all LVGL label values with current sensor readings. Call at ~5 Hz from loop().
// Acquires the LVGL mutex internally — safe to call from the main task.
void display_update();

// Poll the FT3168 touch controller via I²C and inject pointer events into LVGL.
// Call from the main task at ~50 Hz (every 20 ms). Must NOT be called from the LVGL task.
void display_poll_touch();
