#pragma once

// SD card data logger.
// Logs all telemetry to /LOG####.CSV on the TF card (SPI3/HSPI bus).
// Safe no-op if no card is inserted.

void sdlog_init();      // call once from setup()
void sdlog_update();    // call at ~1 Hz from loop()
bool sdlog_is_ready();  // true if card mounted and log file is open
