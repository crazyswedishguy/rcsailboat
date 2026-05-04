// bilge.cpp — bilge water detection and pump relay driver.
//
// Sensor: DYIables bare resistive water sensor (no onboard comparator).
//   Wiring: VCC → 3.3V, GND → GND, SIG → BILGE_SENSOR (GPIO2, ADC1_CH1).
//   Dry: SIG near 0 V (no conduction path). Wet: SIG rises toward VCC as
//   water bridges the traces.  Read with analogRead(); no pull-up needed.
//
// Detection threshold: BILGE_WET_THRESHOLD ADC counts (0–4095, 12-bit).
//   ~500 counts ≈ 0.4 V, well above noise but below any real water contact.
//   Raise this value if dry conditions produce false triggers; lower it if the
//   sensor is slow to respond when wet.
//
// Readings are averaged over BILGE_SAMPLES to reduce ADC noise, then debounced
// over WET_DEBOUNCE_MS before s_water is set.  The s_verified flag becomes true
// once the sensor has triggered at least once this power-cycle, confirming the
// wiring is intact.
//
// The bilge pump is driven by an N-MOSFET gate on GPIO3.  Setting the pin HIGH
// turns the MOSFET on, completing the pump motor circuit from the main battery.

#include "bilge.h"
#include "config.h"
#include <Arduino.h>

#define WET_DEBOUNCE_MS    2000   // ms sensor must read wet continuously before flagging
#define BILGE_SAMPLES         8   // ADC readings averaged per call to bilge_update()
#define BILGE_WET_THRESHOLD 500   // ADC counts (0–4095) above which sensor is considered wet

static bool          s_water     = false;
static bool          s_verified  = false;
static bool          s_pump      = false;
static unsigned long s_wet_start = 0;

static uint16_t bilge_read_adc()
{
    uint32_t sum = 0;
    for (int i = 0; i < BILGE_SAMPLES; i++)
        sum += analogRead(pins::BILGE_SENSOR);
    return (uint16_t)(sum / BILGE_SAMPLES);
}

void bilge_init()
{
    pinMode(pins::BILGE_SENSOR, INPUT);   // no pull-up — sensor drives SIG via VCC/GND
    pinMode(pins::BILGE_PUMP,   OUTPUT);
    digitalWrite(pins::BILGE_PUMP, LOW);
}

void bilge_update()
{
    uint16_t adc  = bilge_read_adc();
    bool raw_wet  = (adc >= BILGE_WET_THRESHOLD);

    if (!raw_wet) {
        s_wet_start = 0;
        s_water     = false;
    } else {
        if (!s_wet_start) s_wet_start = millis();
        if ((millis() - s_wet_start) >= WET_DEBOUNCE_MS) {
            if (!s_water) Serial.printf("bilge: water detected (ADC=%u)\n", adc);
            s_water    = true;
            s_verified = true;
        }
    }

    digitalWrite(pins::BILGE_PUMP, s_pump ? HIGH : LOW);
}

bool bilge_water_detected()  { return s_water; }
bool bilge_sensor_verified() { return s_verified; }
bool bilge_pump_active()     { return s_pump; }

void bilge_pump_set(bool on)
{
    s_pump = on;
    digitalWrite(pins::BILGE_PUMP, on ? HIGH : LOW);
    Serial.printf("bilge: pump %s\n", on ? "ON" : "OFF");
}
