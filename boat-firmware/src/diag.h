// diag.h — I²C peripheral diagnostics: boot-time probe, status table, repair.
//
// diag_init() runs once in setup() right after Wire.begin() and before any
// module init. It probes each I²C address with a plain ACK test; absent
// devices are marked disabled so their modules skip all Wire calls, preventing
// the cascading ESP_ERR_INVALID_STATE (259) errors that follow every NACK.
//
// Repair workflow:
//   Web UI  — GET /repair?id=N runs directly in wifi_ctrl_update() on core 1.
//   Display — Repair button calls diag_request_reprobe() to set a flag; loop()
//             picks it up and calls diag_reprobe() on core 1 (Wire is not
//             thread-safe — never call diag_reprobe() from core 0).
//
// Re-init callbacks are registered by main.cpp before diag_init() is called.
// When a device is found during a reprobe its callback fires immediately so the
// module configures the hardware without needing a reboot.

#pragma once
#include <stdint.h>
#include <stdbool.h>

// ── Device table ──────────────────────────────────────────────────────────────
enum DevId : uint8_t {
    DEV_FT3168  = 0,   // capacitive touch controller (onboard)
    DEV_QMI8658 = 1,   // 6-axis IMU (onboard)
    DEV_PCA9685 = 2,   // PWM servo / ESC driver (external)
    DEV_INA228  = 3,   // current + voltage monitor (external)
    DEV_COUNT   = 4
};

struct DeviceInfo {
    const char *name;    // short identifier, e.g. "PCA9685"
    const char *role;    // human-readable purpose, e.g. "servos"
    uint8_t     addr;    // 7-bit I²C address
    bool        present; // true if last probe received an ACK
    bool        enabled; // false → module skips all Wire calls
};

typedef void (*diag_reinit_fn)();

// Register the re-init function to call when a device is found during reprobe.
// Call before diag_init().
void diag_register_reinit(DevId id, diag_reinit_fn fn);

// Probe all devices. Call once in setup() after Wire.begin(), before all module inits.
void diag_init();

// Re-probe one device and re-init if found.
// MUST be called from core 1 (main loop task) — Wire is not thread-safe.
void diag_reprobe(DevId id);

// Request a reprobe from a FreeRTOS task (core 0) or ISR.
// Sets a flag; loop() calls diag_reprobe() on core 1.
void  diag_request_reprobe(DevId id);
DevId diag_pending_reprobe();   // DEV_COUNT = no request pending
void  diag_clear_reprobe();

bool              diag_ok(DevId id);    // present && enabled
const DeviceInfo *diag_info(DevId id);

// Recover a physically-stuck I²C bus.
// Calls Wire.end(), bit-bangs up to 9 SCL pulses to free any device holding
// SDA low, issues a STOP condition, then calls Wire.begin() + Wire.setClock().
// Use this everywhere instead of bare Wire.end()/Wire.begin() — the plain
// reinit leaves the driver in INVALID_STATE if SDA is still low.
void i2c_recover_bus();
