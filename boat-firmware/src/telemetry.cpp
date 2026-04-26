#include "telemetry.h"
#include "protocol.h"
#include "elrs.h"
#include "imu.h"
#include "power.h"
#include "servos.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

#ifdef GPS_ENABLED
#include "gps.h"
#endif

// ---------------------------------------------------------------------------
// CRSF frame builder
// ---------------------------------------------------------------------------

static uint8_t crsf_crc8(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1);
    }
    return crc;
}

// Builds a complete CRSF frame into buf[]. Returns total byte count.
// buf must be at least (4 + payload_len) bytes.
// Frame layout: [0xC8][len][type][payload...][crc8]
// len = payload_len + 2 (type byte + crc byte)
// CRC covers: type byte + payload bytes
static size_t crsf_build(uint8_t *buf, uint8_t type,
                          const uint8_t *payload, uint8_t payload_len) {
    buf[0] = 0xC8;                // flight-controller address
    buf[1] = payload_len + 2;
    buf[2] = type;
    memcpy(buf + 3, payload, payload_len);
    buf[3 + payload_len] = crsf_crc8(buf + 2, payload_len + 1);
    return 4 + payload_len;
}

static inline void put_u16be(uint8_t *p, uint16_t v) {
    p[0] = v >> 8; p[1] = v & 0xFF;
}
static inline void put_i16be(uint8_t *p, int16_t v) {
    put_u16be(p, (uint16_t)v);
}
static inline void put_u24be(uint8_t *p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = v & 0xFF;
}
static inline void put_i32be(uint8_t *p, int32_t v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF; p[3] =  v        & 0xFF;
}

// ---------------------------------------------------------------------------
// Frame emitters
// ---------------------------------------------------------------------------

// BATTERY_SENSOR (0x08) — 8-byte payload, emit at 2 Hz
static void send_battery() {
    float  voltage = power_voltage_v();
    float  current = power_current_a();
    float  mah     = power_mah_used();
    // Remaining % is a rough estimate based on a 3S LiPo (12.6 V full, 11.1 V nominal).
    // Replace with a proper fuel gauge once we have real flight data.
    float  remaining_pct = constrain((voltage - 11.1f) / (12.6f - 11.1f) * 100.0f, 0.0f, 100.0f);

    uint8_t p[8] = {};
    put_u16be(p + 0, (uint16_t)(voltage * 10.0f));       // 100mV units
    put_u16be(p + 2, (uint16_t)(current * 10.0f));       // 100mA units
    put_u24be(p + 4, (uint32_t)mah);                      // mAh used
    p[7] = (uint8_t)remaining_pct;

    uint8_t frame[16];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_BATTERY, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// ATTITUDE (0x1E) — 6-byte payload, emit at 5 Hz
static void send_attitude() {
    float roll_rad  = imu_roll_deg()  * ((float)M_PI / 180.0f);
    float pitch_rad = imu_pitch_deg() * ((float)M_PI / 180.0f);
    float yaw_rad   = 0.0f;  // Phase 6c: replace with compass_heading_deg() * PI/180

    uint8_t p[6];
    put_i16be(p + 0, (int16_t)(pitch_rad * 10000.0f));
    put_i16be(p + 2, (int16_t)(roll_rad  * 10000.0f));
    put_i16be(p + 4, (int16_t)(yaw_rad   * 10000.0f));

    uint8_t frame[14];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_ATTITUDE, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// SAILBOAT custom frame (0x80) — 8-byte payload, emit at 5 Hz
// Payload: rudder(-10000..10000), sail(0..10000), ESC(-10000..10000), temp(°C*10)
static void send_sailboat() {
    int16_t rudder = (int16_t)(servos_get_commanded(CH_RUDDER_PWM)   * 10000.0f);
    int16_t sail   = (int16_t)(servos_get_commanded(CH_SAIL_PWM)     * 10000.0f);
    int16_t esc    = (int16_t)(servos_get_commanded(CH_ESC_PWM)      * 10000.0f);
    int16_t temp   = (int16_t)(temperatureRead()                      * 10.0f);

    uint8_t p[8];
    put_i16be(p + 0, rudder);
    put_i16be(p + 2, sail);
    put_i16be(p + 4, esc);
    put_i16be(p + 6, temp);

    uint8_t frame[16];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_SAILBOAT, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static unsigned long s_last_battery_ms  = 0;
static unsigned long s_last_attitude_ms = 0;

void telemetry_init() {}

void telemetry_update() {
    unsigned long now = millis();

    if (now - s_last_battery_ms >= 500) {
        s_last_battery_ms = now;
        send_battery();
    }

    if (now - s_last_attitude_ms >= 200) {
        s_last_attitude_ms = now;
        send_attitude();
        send_sailboat();
    }

#ifdef GPS_ENABLED
    static unsigned long s_last_gps_ms = 0;
    if (now - s_last_gps_ms >= 1000) {
        s_last_gps_ms = now;
        telemetry_send_gps();
    }
#endif
}

#ifdef GPS_ENABLED
void telemetry_send_gps() {
    if (!gps_has_fix()) return;

    // GPS frame (0x02) — 15-byte payload
    uint8_t p[15] = {};
    put_i32be(p + 0,  (int32_t)(gps_lat()          * 1e7f));
    put_i32be(p + 4,  (int32_t)(gps_lng()          * 1e7f));
    put_u16be(p + 8,  (uint16_t)(gps_speed_kmh()   * 10.0f));
    put_u16be(p + 10, (uint16_t)(gps_heading_deg()  * 100.0f));
    put_u16be(p + 12, (uint16_t)(gps_altitude_m()  + 1000.0f));
    p[14] = (uint8_t)gps_satellites();

    uint8_t frame[24];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_GPS, p, sizeof(p));
    elrs_send_frame(frame, len);
}
#endif
