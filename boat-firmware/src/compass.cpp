#include "compass.h"
#ifdef COMPASS_ENABLED

#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_HMC5883_U.h>
#include <math.h>

static Adafruit_HMC5883_Unified s_mag(12345);   // arbitrary sensor ID
static bool  s_ok      = false;
static float s_heading = 0.0f;

void compass_init()
{
    s_ok = s_mag.begin();
    if (!s_ok) Serial.println("compass: HMC5883L not found at 0x1E");
}

void compass_update()
{
    if (!s_ok) return;
    sensors_event_t event;
    s_mag.getEvent(&event);

    float h = atan2f(event.magnetic.y, event.magnetic.x);
    if (h < 0.0f) h += 2.0f * (float)M_PI;
    s_heading = h * (180.0f / (float)M_PI);
}

float compass_heading_deg() { return s_heading; }
bool  compass_ok()          { return s_ok; }

#endif  // COMPASS_ENABLED
