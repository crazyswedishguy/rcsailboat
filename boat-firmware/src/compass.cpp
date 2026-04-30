// compass.cpp — HMC5883L magnetometer implementation (COMPASS_ENABLED builds only).
//
// Uses the Adafruit HMC5883 Unified library, which abstracts the register-level
// I²C communication and returns calibrated magnetic field measurements in µT.
// The HMC5883L lives on the BN-880 GPS module breakout (I²C address 0x1E) and
// shares the main I²C bus (GPIO47/48) with the PCA9685, INA219, FT3168, and QMI8658.
//
// Heading computation:
//   heading = atan2(Y, X)         — angle of magnetic north in the sensor plane
//   normalised to 0–360° by adding 2π when the result is negative.
//
// Important limitations:
//   - No magnetic declination correction is applied. True north = magnetic north + declination.
//     Declination varies by location (e.g. ~0° in parts of UK, ~15°W in eastern US).
//     Add a constant offset if accurate bearing is needed.
//   - The sensor must be mounted level for accurate 2D heading. Tilted mounting
//     introduces cosine error in the heading proportional to the tilt angle.
//   - No hard/soft iron calibration. Nearby ferrous metal (the motor, ESC wires,
//     battery) will bias the readings. Calibrate after final installation.

#include "compass.h"
#ifdef COMPASS_ENABLED

#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_HMC5883_U.h>
#include <math.h>

// Arbitrary sensor ID passed to the Adafruit Unified Sensor framework.
// Only matters if multiple Adafruit sensors are registered simultaneously.
static Adafruit_HMC5883_Unified s_mag(12345);

static bool  s_ok      = false;    // false if sensor not found at init
static float s_heading = 0.0f;    // most recent heading in degrees (0 = N, 90 = E)

void compass_init()
{
    // begin() probes I²C address 0x1E and verifies the HMC5883L ID registers.
    s_ok = s_mag.begin();
    if (!s_ok) {
        Serial.println("compass: HMC5883L not found at 0x1E — check BN-880 I²C wiring");
    } else {
        Serial.println("compass: HMC5883L ready");
    }
}

void compass_update()
{
    if (!s_ok) return;

    sensors_event_t event;
    s_mag.getEvent(&event);  // fills event.magnetic.x/y/z in µT

    // 2D heading: atan2 gives –π..+π; shift negative results into 0..2π range.
    float h = atan2f(event.magnetic.y, event.magnetic.x);
    if (h < 0.0f) h += 2.0f * (float)M_PI;
    s_heading = h * (180.0f / (float)M_PI);  // convert radians to degrees
}

// Returns magnetic heading 0–360° (0 = North, 90 = East, 180 = South, 270 = West).
float compass_heading_deg() { return s_heading; }

// True if the HMC5883L was found and configured successfully at boot.
bool  compass_ok()          { return s_ok; }

#endif  // COMPASS_ENABLED
