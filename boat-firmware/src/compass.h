#pragma once
#ifdef COMPASS_ENABLED

// HMC5883L compass interface (BN-880 GPS module, I²C 0x1E, shared bus).
// Heading is magnetic — no declination correction applied.

void  compass_init();           // detect sensor; logs error if absent
void  compass_update();         // read one sample; call from loop() at ~10 Hz
float compass_heading_deg();    // magnetic heading 0–360° (0 = N, clockwise)
bool  compass_ok();             // true if sensor was found at init

#endif  // COMPASS_ENABLED
