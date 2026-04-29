#pragma once
#include <stdint.h>

enum class CtrlMode { WIFI, ELRS };

// Lifecycle
void wifi_ctrl_init();      // call once from setup() — starts in WiFi AP mode
void wifi_ctrl_update();    // call every loop() — serves HTTP, applies mode switches

// Mode management (safe to call from any FreeRTOS task — just sets a flag)
void     wifi_ctrl_set_mode(CtrlMode m);
CtrlMode wifi_ctrl_mode();

// WiFi status (reads are safe from any task)
uint8_t  wifi_ctrl_clients();           // stations currently connected to AP
bool     wifi_ctrl_active();            // true when WiFi AP is running

// Control values from web UI (read by main loop when mode == WIFI)
float    wifi_ctrl_rudder();
float    wifi_ctrl_sail();
float    wifi_ctrl_throttle();
bool     wifi_ctrl_armed();
bool     wifi_ctrl_timed_out();         // no /control request in last 500 ms → failsafe
unsigned long wifi_ctrl_last_cmd_ms();
