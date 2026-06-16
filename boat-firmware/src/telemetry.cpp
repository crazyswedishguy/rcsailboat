// telemetry.cpp — CRSF telemetry frame builder and scheduled emitter.
//
// Collects data from the power, IMU, compass, GPS, and servo modules and packs
// it into CRSF frames, which are passed to elrs_send_frame() for transmission.
//
// Frame emission schedule (managed with millis() timers in telemetry_update()):
//   BATTERY_SENSOR (0x08) every 500 ms (2 Hz)
//   ATTITUDE       (0x1E) every 200 ms (5 Hz)
//   SAILBOAT       (0x80) every 200 ms (5 Hz) — emitted on the same tick as ATTITUDE
//   GPS            (0x02) every 1000 ms (1 Hz) — only when GPS_ENABLED and fix available
//
// CRSF frame layout (all frames):
//   Byte 0:   0xC8 — CRSF_SYNC / flight-controller address
//   Byte 1:   length — number of bytes following (payload_len + 2: type byte + CRC byte)
//   Byte 2:   type — frame type identifier (see protocol.h)
//   Bytes 3…: payload — big-endian, frame-type specific
//   Last byte: CRC-8/DVB-S2 (polynomial 0xD5) over type + payload bytes
//
// Scaling conventions for CRSF field values:
//   Angles (attitude): float radians × 10000 → int16_t, transmitted big-endian
//   Voltage: float volts × 10 → uint16_t (100 mV units)
//   Current: float amps × 10 → uint16_t (100 mA units)
//   Servo positions: float –1.0..+1.0 × 10000 → int16_t

#include "telemetry.h"
#include "protocol.h"
#include "elrs.h"
#include <crsf_protocol.h>  // CRSF_FRAMETYPE_GPS, CRSF_FRAMETYPE_BATTERY_SENSOR, CRSF_FRAMETYPE_ATTITUDE
#include "imu.h"
#include "power.h"
#include "servos.h"
#include "config.h"
#include "diag.h"
#include "bilge.h"
#include "failsafe.h"
#include "sdlog.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

#ifdef GPS_ENABLED
#include "gps.h"
#endif
#ifdef COMPASS_ENABLED
#include "compass.h"
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
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_BATTERY_SENSOR, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// ATTITUDE (0x1E) — 6-byte payload, emit at 5 Hz
static void send_attitude() {
    float roll_rad  = imu_roll_deg()  * ((float)M_PI / 180.0f);
    float pitch_rad = imu_pitch_deg() * ((float)M_PI / 180.0f);
#ifdef COMPASS_ENABLED
    float yaw_rad = compass_heading_deg() * ((float)M_PI / 180.0f);
#else
    float yaw_rad = 0.0f;
#endif

    uint8_t p[6];
    put_i16be(p + 0, (int16_t)(pitch_rad * 10000.0f));
    put_i16be(p + 2, (int16_t)(roll_rad  * 10000.0f));
    put_i16be(p + 4, (int16_t)(yaw_rad   * 10000.0f));

    uint8_t frame[14];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_ATTITUDE, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// SAILBOAT custom frame (0x80) — 8-byte payload, emit at 5 Hz
// Payload: rudder(-10000..10000), sail(0..10000), ESC(-10000..10000),
//          temp(°C*10), status byte (see protocol.h SB_STATUS_* bits).
static void send_sailboat() {
    int16_t rudder = (int16_t)(servos_get_commanded(pwm_ch::RUDDER)     * 10000.0f);
    int16_t sail   = (int16_t)(servos_get_commanded(pwm_ch::SAIL_WINCH) * 10000.0f);
    int16_t esc    = (int16_t)(servos_get_commanded(pwm_ch::MOTOR_ESC)  * 10000.0f);
    int16_t temp   = (int16_t)(temperatureRead()                      * 10.0f);

    uint8_t status = 0;
    if (imu_is_capsized())      status |= (1 << SB_STATUS_CAPSIZED);
    if (bilge_water_detected()) status |= (1 << SB_STATUS_BILGE_WET);
    if (bilge_pump_active())    status |= (1 << SB_STATUS_PUMP);
    if (failsafe_armed())       status |= (1 << SB_STATUS_ARMED);
    if (failsafe_active())      status |= (1 << SB_STATUS_FAILSAFE);

    uint8_t p[9];
    put_i16be(p + 0, rudder);
    put_i16be(p + 2, sail);
    put_i16be(p + 4, esc);
    put_i16be(p + 6, temp);
    p[8] = status;

    uint8_t frame[16];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_SAILBOAT, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// DEVICE_STATUS (0x81) — 3-byte payload, emit at 0.2 Hz (every 5 s).
//
// Bit layout (2 bits per device, LSB-first):
//   [1:0]   ft     — FT3168 touch controller
//   [3:2]   qmi    — QMI8658 IMU
//   [5:4]   pca    — PCA9685 servo driver
//   [7:6]   ina    — INA228 current/voltage sensor
//   [9:8]   sd     — SD card logger
//   [11:10] bilge  — bilge float switch
//   [13:12] pump   — bilge pump (reserved, absent)
//   [15:14] rudder — rudder servo (follows PCA9685)
//   [17:16] winch  — sail winch servo (follows PCA9685)
//   [19:18] esc    — motor ESC (follows PCA9685)
//   [23:20] reserved (0)
//
// Level values: 0=absent, 1=ok, 2=warn, 3=error
// Bilge special: 2=wet (alert), 3=unverified (dry but never triggered this session)
static void send_devices() {
    uint8_t ft_l  = diag_ok(DEV_FT3168)  ? 1 : 0;
    uint8_t qmi_l = diag_ok(DEV_QMI8658) ? 1 : 0;
    uint8_t pca_l = diag_ok(DEV_PCA9685) ? 1 : 0;
    uint8_t ina_l = diag_ok(DEV_INA228)  ? 1 : 0;
    uint8_t sd_l  = sdlog_is_ready()      ? 1 : 0;

    bool    wet      = bilge_water_detected();
    bool    verified = bilge_sensor_verified();
    uint8_t bilge_l  = wet ? 2 : (verified ? 1 : 3);  // warn=wet, ok=dry+verified, error=unverified

    uint8_t pump_l = 0;   // no pump driver — absent
    uint8_t act_l  = pca_l;   // actuators follow servo driver

    uint32_t bits = 0;
    bits |= (uint32_t)(ft_l    & 3) <<  0;
    bits |= (uint32_t)(qmi_l   & 3) <<  2;
    bits |= (uint32_t)(pca_l   & 3) <<  4;
    bits |= (uint32_t)(ina_l   & 3) <<  6;
    bits |= (uint32_t)(sd_l    & 3) <<  8;
    bits |= (uint32_t)(bilge_l & 3) << 10;
    bits |= (uint32_t)(pump_l  & 3) << 12;
    bits |= (uint32_t)(act_l   & 3) << 14;  // rudder
    bits |= (uint32_t)(act_l   & 3) << 16;  // winch
    bits |= (uint32_t)(act_l   & 3) << 18;  // esc

    uint8_t p[3] = {
        (uint8_t)( bits        & 0xFF),
        (uint8_t)((bits >>  8) & 0xFF),
        (uint8_t)((bits >> 16) & 0xFF),
    };

    uint8_t frame[11];
    size_t  len = crsf_build(frame, CRSF_FRAMETYPE_DEVICES, p, sizeof(p));
    elrs_send_frame(frame, len);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static unsigned long s_last_battery_ms  = 0;
static unsigned long s_last_attitude_ms = 0;
static unsigned long s_last_sailboat_ms = 0;
static unsigned long s_last_devices_ms  = 0;

// ATTITUDE runs fast (24 Hz) for smooth heel/pitch gauges; the link is sized
// for it (250 Hz / 1:4 telemetry ratio — see docs/protocol.md). SAILBOAT and
// DEVICES stay slow on their own timers so attitude can run independently.
#define ATTITUDE_PERIOD_MS  42   // ~24 Hz
#define SAILBOAT_PERIOD_MS  200  // 5 Hz
#define BATTERY_PERIOD_MS   500  // 2 Hz
#define DEVICES_PERIOD_MS   5000 // 0.2 Hz

void telemetry_init() {}

void telemetry_update() {
    unsigned long now = millis();

    if (now - s_last_battery_ms >= BATTERY_PERIOD_MS) {
        s_last_battery_ms = now;
        send_battery();
    }

    if (now - s_last_attitude_ms >= ATTITUDE_PERIOD_MS) {
        s_last_attitude_ms = now;
        send_attitude();
    }

    if (now - s_last_sailboat_ms >= SAILBOAT_PERIOD_MS) {
        s_last_sailboat_ms = now;
        send_sailboat();
    }

    if (now - s_last_devices_ms >= DEVICES_PERIOD_MS) {
        s_last_devices_ms = now;
        send_devices();
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
