#pragma once
#include <stdint.h>

// SD card data logger.
// Logs all telemetry to /LOG####.CSV on the TF card (SPI3/HSPI bus).
// Safe no-op if no card is inserted.

void sdlog_init();          // call once from setup()
void sdlog_update();        // call at ~1 Hz from loop()
bool sdlog_is_ready();      // true if card mounted and log file is open
bool sdlog_mount_failed();  // true if card present but failed to mount (e.g. exFAT)
uint64_t sdlog_card_mb();   // total card size in MB (0 if not ready)
