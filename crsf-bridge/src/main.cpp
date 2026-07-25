// main.cpp — XIAO ESP32-S3 dual-mode GFSK bridge firmware.
//
// Replaces the previous Ranger Micro / CRSF-UART radio link with a direct
// SX1262 GFSK link between this XIAO and the boat ESP32-S3.
//
// Two operating modes, switched dynamically based on Pi heartbeat detection:
//
//   Mode 2 — Standalone AP  (default at boot; Pi absent)
//     • Brings up WiFi AP "Mistral-2" and serves the shared embedded control page.
//     • Builds CRSF RC_CHANNELS_PACKED frames and transmits them over GFSK at 50 Hz.
//     • Receives CRSF telemetry frames from the boat over GFSK; exposes them via
//       HTTP JSON endpoints (same API surface as the boat's own wifi_ctrl.cpp).
//     • Control timeout: if no /control hit for 500 ms, output goes to neutral/disarmed.
//
//   Mode 3 — Pi bridge  (active while Pi's elrs_bridge.py heartbeat is detected)
//     • Tears down the AP; becomes a frame-aware USB-CDC ↔ GFSK bridge.
//     • Pi sends CRSF RC frames over USB-CDC; XIAO forwards them over GFSK.
//     • Boat sends CRSF telemetry over GFSK; XIAO forwards bytes to Pi via USB-CDC.
//     • Pi heartbeat frames (type 0x7E) are stripped — not forwarded over GFSK.
//     • 2 s silence from Pi → revert to Mode 2.
//
//   Transition safety: commanded state resets to neutral/disarmed on every mode switch.
//
// SX1262 wiring (XIAO ESP32-S3, hardware SPI):
//   SPI bus: CLK=D8(GPIO7)  MISO=D9(GPIO8)  MOSI=D10(GPIO9)
//   Control: CS=D3(GPIO4)   RESET=D2(GPIO3)  DIO1=D1(GPIO2)  BUSY=D0(GPIO1)
//   RF switch (Waveshare module): RXEN=D6(GPIO43)  TXEN=D7(GPIO44)
//
// GFSK protocol: raw CRSF frames are sent as GFSK packet payloads.
//   XIAO→Boat: CRSF RC_CHANNELS_PACKED (type 0x16), 26 bytes, at 50 Hz.
//   Boat→XIAO: any CRSF telemetry frame (GPS 0x02, BATTERY 0x08, ATTITUDE 0x1E, …)
//
// Shared UI headers (from shared/web/ via -I"${PROJECT_DIR}/../shared"):
//   web/control_page.h — HTML_PAGE[] PROGMEM (shared with boat's wifi_ctrl.cpp)
//   web/map_page.h     — MAP_HTML[] PROGMEM

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <RadioLib.h>
#include <math.h>
#include <string.h>

#include "web/control_page.h"
#include "web/map_page.h"

// ── SX1262 pin assignments (XIAO ESP32-S3, hardware SPI on SPI2) ──────────────
namespace cfg {
    // Radio hardware
    constexpr int SX_CLK   = 7;   // D8 — SPI clock
    constexpr int SX_MISO  = 8;   // D9 — SPI MISO
    constexpr int SX_MOSI  = 9;   // D10 — SPI MOSI
    constexpr int SX_CS    = 4;   // D3 — chip select (active low)
    constexpr int SX_RESET = 3;   // D2 — module reset (active low)
    constexpr int SX_DIO1  = 2;   // D1 — interrupt: TX/RX done
    constexpr int SX_BUSY  = 1;   // D0 — busy flag
    constexpr int SX_RXEN  = 43;  // D6 — RF switch: enable LNA (RX mode)
    constexpr int SX_TXEN  = 44;  // D7 — RF switch: enable PA (TX mode)

    // GFSK link parameters — must match boat-firmware/src/config.h FSK_* constants
    constexpr float    FSK_FREQ_MHZ      = 915.0f;
    constexpr float    FSK_BIT_RATE_KBPS = 150.0f;
    constexpr float    FSK_FREQ_DEV_KHZ  = 75.0f;
    constexpr float    FSK_RX_BW_KHZ     = 312.0f;
    constexpr int8_t   FSK_POWER_DBM     = 14;
    constexpr uint16_t FSK_PREAMBLE_BITS = 16;

    // Application timing
    constexpr char     AP_SSID[]        = "Mistral-2";
    constexpr char     AP_PASS[]        = "readyabout";
    constexpr uint32_t CTRL_TIMEOUT_MS  = 500;    // servo timeout
    constexpr uint32_t MODE_REQUEST_TIMEOUT_MS = 30000;  // CH_MODE presence
    // Minimum gap between RC transmissions — NOT an enforced cycle time.
    // ap_radio_tick() itself (TX + TELEM_WAIT_MS listen) already takes
    // longer than this in practice — see TELEM_WAIT_MS below — so the real
    // achieved RC rate is set by that actual duration, not this constant.
    constexpr uint32_t TX_PERIOD_MS     = 20;
    constexpr uint32_t HB_TIMEOUT_MS    = 2000;   // Pi absence → revert to AP mode
    // Max time to wait for the boat's telemetry reply before giving up and
    // moving on. Sized to comfortably cover the boat's measured reply time
    // across all telemetry frame types (~2-3ms on real hardware over GFSK —
    // was ~11-15ms under LoRa, see docs/lora-bringup.md — with margin for
    // jitter). Widening this trades RC rate for telemetry reliability: every
    // cycle waits up to this long when nothing is queued, since the XIAO
    // can't know in advance whether the boat has a reply coming.
    constexpr uint32_t TELEM_WAIT_MS    = 6;
}

// ── RadioLib objects ───────────────────────────────────────────────────────────
// Use the framework's default global SPI object directly — SPIClass has no
// constructor taking an SPIClass&, so `SPIClass s_spi(SPI)` silently invoked
// the compiler-generated *copy* constructor instead of aliasing it. That
// copied SPI's internal FreeRTOS mutex handle at static-init time, before
// SPI's own constructor (in a different translation unit, unspecified init
// order) had necessarily run yet — leaving the copy's mutex handle null and
// crashing on the first beginTransaction() with
// "assert failed: xQueueSemaphoreTake queue.c:1709".
static Module     s_module(cfg::SX_CS, cfg::SX_DIO1, cfg::SX_RESET,
                            cfg::SX_BUSY, SPI);
static SX1262     s_radio(&s_module);
static volatile bool s_pkt_ready = false;  // set by DIO1 ISR
static void IRAM_ATTR on_dio1() { s_pkt_ready = true; }

// ── CRSF frame type constants ──────────────────────────────────────────────────
constexpr uint8_t CRSF_SYNC            = 0xC8;
constexpr uint8_t CRSF_TYPE_RC         = 0x16;
constexpr uint8_t CRSF_TYPE_GPS        = 0x02;
constexpr uint8_t CRSF_TYPE_BATTERY    = 0x08;
constexpr uint8_t CRSF_TYPE_LINK_STATS = 0x14;
constexpr uint8_t CRSF_TYPE_ATTITUDE   = 0x1E;
constexpr uint8_t CRSF_TYPE_SAILBOAT   = 0x80;
constexpr uint8_t CRSF_TYPE_DEVICES    = 0x81;
constexpr uint8_t CRSF_TYPE_HEARTBEAT  = 0x7E;  // Pi liveness signal (stripped, not forwarded)

constexpr uint16_t CRSF_CH_MIN    = 172;
constexpr uint16_t CRSF_CH_CENTER = 992;
constexpr uint16_t CRSF_CH_MAX    = 1811;

// Channel indices — 0-based, matching boat-firmware/src/protocol.h
constexpr int CH_RUDDER   = 0;
constexpr int CH_SAIL     = 1;
constexpr int CH_THROTTLE = 2;
constexpr int CH_ARM      = 3;
constexpr int CH_MODE     = 4;
constexpr int CH_RESTART  = 5;
constexpr int CH_PUMP     = 6;

// ── CRC-8/DVB-S2 (polynomial 0xD5) ───────────────────────────────────────────
static uint8_t crsf_crc8(const uint8_t *buf, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1);
    }
    return crc;
}

// ── RC frame builder ──────────────────────────────────────────────────────────

// Map –1.0..+1.0 → CRSF_CH_MIN..CRSF_CH_MAX, centred at 992.
static uint16_t ch_centered(float v)
{
    int val = (int)roundf(v * (float)(CRSF_CH_MAX - CRSF_CH_CENTER) + CRSF_CH_CENTER);
    if (val < CRSF_CH_MIN) val = CRSF_CH_MIN;
    if (val > CRSF_CH_MAX) val = CRSF_CH_MAX;
    return (uint16_t)val;
}

// Map 0.0..+1.0 → CRSF_CH_MIN..CRSF_CH_MAX.
static uint16_t ch_unipolar(float v)
{
    int val = (int)roundf(v * (float)(CRSF_CH_MAX - CRSF_CH_MIN) + CRSF_CH_MIN);
    if (val < CRSF_CH_MIN) val = CRSF_CH_MIN;
    if (val > CRSF_CH_MAX) val = CRSF_CH_MAX;
    return (uint16_t)val;
}

// Pack 16 × 11-bit channel values LSB-first into out[22].
static void pack_channels(const uint16_t ch[16], uint8_t out[22])
{
    memset(out, 0, 22);
    for (int i = 0; i < 16; i++) {
        int      bit_pos  = i * 11;
        int      byte_idx = bit_pos >> 3;
        int      bit_off  = bit_pos & 7;
        uint16_t val      = ch[i] & 0x7FF;
        out[byte_idx]     |= (uint8_t)((val << bit_off) & 0xFF);
        out[byte_idx + 1] |= (uint8_t)((val >> (8 - bit_off)) & 0xFF);
        if (bit_off > 5)
            out[byte_idx + 2] |= (uint8_t)((val >> (16 - bit_off)) & 0xFF);
    }
}

// ── Commanded control state ────────────────────────────────────────────────────
static float         s_rudder      = 0.0f;
static float         s_sail        = 0.0f;
static float         s_throttle    = 0.0f;
static bool          s_armed       = false;
static bool          s_pump        = false;
static unsigned long s_last_cmd_ms = 0;

// Build a 26-byte CRSF RC Channels Packed frame from current commanded state.
// Applies control timeout: if no /control hit recently, output neutral/disarmed.
static void build_rc_frame(uint8_t buf[26])
{
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = CRSF_CH_CENTER;

    bool timed_out = (millis() - s_last_cmd_ms) > cfg::CTRL_TIMEOUT_MS;
    bool active    = s_armed && !timed_out;

    ch[CH_RUDDER]   = ch_centered(active ? s_rudder : 0.0f);
    ch[CH_SAIL]     = ch_unipolar(s_sail);
    ch[CH_THROTTLE] = ch_centered(active ? s_throttle : 0.0f);
    ch[CH_ARM]      = active ? CRSF_CH_MAX : CRSF_CH_MIN;
    // CH_MODE signals the boat to switch into GFSK mode.
    // Uses the looser MODE_REQUEST_TIMEOUT_MS so brief polling gaps don't
    // flap the boat's WiFi AP up/down (see docs/protocol.md).
    bool mode_timed_out = (millis() - s_last_cmd_ms) > cfg::MODE_REQUEST_TIMEOUT_MS;
    ch[CH_MODE]    = mode_timed_out ? CRSF_CH_CENTER : CRSF_CH_MAX;
    ch[CH_RESTART] = CRSF_CH_MIN;
    ch[CH_PUMP]    = s_pump ? CRSF_CH_MAX : CRSF_CH_MIN;

    uint8_t payload[22];
    pack_channels(ch, payload);

    // Frame: [0xEE][24][0x16][22-byte payload][CRC]
    // Destination 0xEE = CRSF_ADDRESS_CRSF_TRANSMITTER (kept for protocol
    // compatibility with boat-firmware's CRSF parser).
    buf[0] = 0xEE;
    buf[1] = 24;
    buf[2] = CRSF_TYPE_RC;
    memcpy(buf + 3, payload, 22);
    buf[25] = crsf_crc8(buf + 2, 23);
}

// ── Telemetry state ────────────────────────────────────────────────────────────
struct TelState {
    float  voltage_v    = 0.0f;
    float  current_a    = 0.0f;
    float  mah_used     = 0.0f;
    int    battery_pct  = 0;
    float  pitch_deg    = 0.0f;
    float  roll_deg     = 0.0f;
    float  yaw_deg      = 0.0f;
    float  rud_cmd      = 0.0f;
    float  sail_cmd     = 0.0f;
    float  throttle_cmd = 0.0f;
    float  mcu_temp_c   = 0.0f;
    bool   capsized     = false;
    bool   bilge_wet    = false;
    bool   pump_active  = false;
    bool   boat_armed   = false;
    bool   failsafe     = false;
    int    rssi_up      = 0;
    int    lq_up        = 0;
    uint8_t dev_qmi  = 0, dev_pca = 0, dev_ina = 0, dev_sd = 0;
    uint8_t dev_bilge = 0, dev_rudder = 0, dev_winch = 0, dev_esc = 0;
    double gps_lat    = 0.0;
    double gps_lng    = 0.0;
    float  gps_alt_m  = 0.0f;
    float  gps_spd_kn = 0.0f;
    float  gps_hdg_deg= 0.0f;
    int    gps_sats   = 0;
    bool   gps_fix    = false;
};
static TelState s_tel;
// millis() of the last successfully decoded telemetry packet from the boat.
static unsigned long s_telem_last_ms = 0;

// ── Telemetry frame decoders ──────────────────────────────────────────────────
static inline int16_t  get_i16be(const uint8_t *p) { return (int16_t)((p[0]<<8)|p[1]); }
static inline uint16_t get_u16be(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static inline uint32_t get_u24be(const uint8_t *p) {
    return ((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
}

static void decode_battery(const uint8_t *p, size_t len) {
    if (len < 8) return;
    s_tel.voltage_v   = get_u16be(p + 0) / 10.0f;
    s_tel.current_a   = get_u16be(p + 2) / 10.0f;
    s_tel.mah_used    = (float)get_u24be(p + 4);
    s_tel.battery_pct = p[7];
}

// Custom 3-byte encoding (PROTOCOL_VERSION 4) — see telemetry.cpp send_attitude().
// pitch/roll: int8, ~1.4°/LSB (±180° range). yaw/heading: uint8, ~1.4°/LSB, 0–360°.
static void decode_attitude(const uint8_t *p, size_t len) {
    if (len < 3) return;
    s_tel.pitch_deg = (float)(int8_t)p[0] * (180.0f / 127.0f);
    s_tel.roll_deg  = (float)(int8_t)p[1] * (180.0f / 127.0f);
    s_tel.yaw_deg   = (float)p[2]         * (360.0f / 256.0f);
}

static void decode_sailboat(const uint8_t *p, size_t len) {
    if (len < 9) return;
    s_tel.rud_cmd      = get_i16be(p + 0) / 10000.0f;
    s_tel.sail_cmd     = get_i16be(p + 2) / 10000.0f;
    s_tel.throttle_cmd = get_i16be(p + 4) / 10000.0f;
    s_tel.mcu_temp_c   = get_i16be(p + 6) / 10.0f;
    uint8_t st = p[8];
    s_tel.capsized    = (st >> 0) & 1;
    s_tel.bilge_wet   = (st >> 1) & 1;
    s_tel.pump_active = (st >> 2) & 1;
    s_tel.boat_armed  = (st >> 3) & 1;
    s_tel.failsafe    = (st >> 4) & 1;
}

static void decode_link_stats(const uint8_t *p, size_t len) {
    if (len < 10) return;
    uint8_t active_ant = p[4];
    s_tel.rssi_up = -(int)(active_ant == 0 ? p[0] : p[1]);
    s_tel.lq_up   = p[2];
}

static void decode_devices(const uint8_t *p, size_t len) {
    if (len < 3) return;
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    s_tel.dev_qmi    = (bits >> 2)  & 0x3;
    s_tel.dev_pca    = (bits >> 4)  & 0x3;
    s_tel.dev_ina    = (bits >> 6)  & 0x3;
    s_tel.dev_sd     = (bits >> 8)  & 0x3;
    s_tel.dev_bilge  = (bits >> 10) & 0x3;
    s_tel.dev_rudder = (bits >> 14) & 0x3;
    s_tel.dev_winch  = (bits >> 16) & 0x3;
    s_tel.dev_esc    = (bits >> 18) & 0x3;
}

static void decode_gps(const uint8_t *p, size_t len) {
    if (len < 15) return;
    int32_t  lat_raw = ((int32_t)p[0]<<24)|((int32_t)p[1]<<16)|((int32_t)p[2]<<8)|p[3];
    int32_t  lng_raw = ((int32_t)p[4]<<24)|((int32_t)p[5]<<16)|((int32_t)p[6]<<8)|p[7];
    uint16_t spd_raw = ((uint16_t)p[8] <<8)|p[9];
    uint16_t hdg_raw = ((uint16_t)p[10]<<8)|p[11];
    uint16_t alt_raw = ((uint16_t)p[12]<<8)|p[13];
    s_tel.gps_lat    = lat_raw / 1e7;
    s_tel.gps_lng    = lng_raw / 1e7;
    s_tel.gps_spd_kn = spd_raw / 10.0f / 1.852f;
    s_tel.gps_hdg_deg= hdg_raw / 100.0f;
    s_tel.gps_alt_m  = (float)alt_raw - 1000.0f;
    s_tel.gps_sats   = p[14];
    s_tel.gps_fix    = (p[14] >= 3);
}

// Parse one received GFSK packet as a CRSF telemetry frame and dispatch it.
// Called for every packet received from the boat.
static void process_telem_packet(const uint8_t *buf, size_t len)
{
    if (len < 4) return;  // shortest valid CRSF: sync(1)+len(1)+type(1)+crc(1)
    uint8_t frame_len = buf[1];
    size_t  total     = 2u + frame_len;
    if (total > len) return;
    if (crsf_crc8(buf + 2, frame_len - 1) != buf[total - 1]) return;

    uint8_t        type    = buf[2];
    const uint8_t *payload = buf + 3;
    size_t         plen    = (size_t)(frame_len - 2);
    s_telem_last_ms = millis();
    Serial.printf("bridge: telem RX type=0x%02X len=%u RSSI=%.0fdBm\n",
                  type, (unsigned)plen, (double)s_radio.getRSSI());

    switch (type) {
        case CRSF_TYPE_GPS:        decode_gps(payload, plen);        break;
        case CRSF_TYPE_BATTERY:    decode_battery(payload, plen);    break;
        case CRSF_TYPE_ATTITUDE:   decode_attitude(payload, plen);   break;
        case CRSF_TYPE_SAILBOAT:   decode_sailboat(payload, plen);   break;
        case CRSF_TYPE_LINK_STATS: decode_link_stats(payload, plen); break;
        case CRSF_TYPE_DEVICES:    decode_devices(payload, plen);    break;
        default: break;
    }
}

// ── Pi USB buffer — used in both modes ────────────────────────────────────────
static uint8_t s_pi_buf[256];
static size_t  s_pi_len = 0;

// Scan the Pi USB buffer for heartbeat frames (Mode 2 → 3 transition trigger).
// Returns true if at least one heartbeat (0x7E) was seen.
static bool scan_pi_for_heartbeat()
{
    bool saw_hb = false;
    while (s_pi_len >= 2) {
        while (s_pi_len > 0 && s_pi_buf[0] != CRSF_SYNC)
            memmove(s_pi_buf, s_pi_buf + 1, --s_pi_len);
        if (s_pi_len < 2) break;
        uint8_t frame_len = s_pi_buf[1];
        size_t  total     = 2u + frame_len;
        if (s_pi_len < total) break;
        if (s_pi_buf[2] == CRSF_TYPE_HEARTBEAT) saw_hb = true;
        memmove(s_pi_buf, s_pi_buf + total, s_pi_len - total);
        s_pi_len -= total;
    }
    return saw_hb;
}

// ── GPS track buffer ───────────────────────────────────────────────────────────
struct GpsPt { float lat, lng; };
static GpsPt         s_track[200];
static int           s_track_len     = 0;
static unsigned long s_track_last_ms = 0;

// ── Web server (Mode 2 only) ───────────────────────────────────────────────────
static WebServer s_srv(80);

static void handle_root()  { s_srv.send_P(200, "text/html; charset=utf-8", HTML_PAGE); }
static void handle_map()   { s_srv.send_P(200, "text/html; charset=utf-8", MAP_HTML); }

static void handle_track() {
    if (s_track_len == 0) { s_srv.send(200, "application/json", "[]"); return; }
    String json;
    json.reserve(s_track_len * 22 + 4);
    json = "[";
    for (int i = 0; i < s_track_len; i++) {
        char pt[24];
        snprintf(pt, sizeof(pt), "[%.6f,%.6f]", s_track[i].lat, s_track[i].lng);
        if (i) json += ",";
        json += pt;
    }
    json += "]";
    s_srv.send(200, "application/json", json);
}

static void handle_control() {
    float r  = s_srv.arg("r").toFloat() / 100.0f;
    float sl = s_srv.arg("s").toFloat() / 100.0f;
    float t  = s_srv.arg("t").toFloat() / 100.0f;
    bool  a  = s_srv.arg("a") == "1";

    s_rudder   = constrain(r,  -1.0f, 1.0f);
    s_sail     = constrain(sl,  0.0f, 1.0f);
    s_throttle = a ? constrain(t, -1.0f, 1.0f) : 0.0f;
    s_armed    = a;
    s_last_cmd_ms = millis();
    s_srv.send(200, "text/plain", "OK");
}

static void handle_telemetry() {
    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"v\":%.2f,\"a\":%.2f,\"mah\":%.0f,\"pct\":%d"
        ",\"temp\":%.0f,\"roll\":%.1f,\"pitch\":%.1f,\"yaw\":%.0f",
        s_tel.voltage_v, s_tel.current_a, s_tel.mah_used, s_tel.battery_pct,
        s_tel.mcu_temp_c, s_tel.roll_deg, s_tel.pitch_deg, s_tel.yaw_deg);
    n += snprintf(buf + n, sizeof(buf) - n, ",\"sats\":%d,\"fix\":%s",
                  s_tel.gps_sats, s_tel.gps_fix ? "true" : "false");
    if (s_tel.gps_fix)
        n += snprintf(buf + n, sizeof(buf) - n,
            ",\"lat\":%.6f,\"lng\":%.6f,\"alt\":%.0f,\"speed\":%.1f,\"hdg\":%.0f",
            s_tel.gps_lat, s_tel.gps_lng, s_tel.gps_alt_m,
            s_tel.gps_spd_kn, s_tel.gps_hdg_deg);
    snprintf(buf + n, sizeof(buf) - n, "}");
    s_srv.send(200, "application/json", buf);
}

static void handle_diag() {
    char buf[768];
    int  n = 0;
    n += snprintf(buf, sizeof(buf), "[");

    bool link_ok = s_telem_last_ms != 0 && (millis() - s_telem_last_ms) < 5000;
    const char *no_link = "No GFSK link";

    auto bool_field = [&](const char *id, uint8_t val, const char *ok_stat, const char *absent_stat) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"id\":\"%s\",\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            n > 1 ? "," : "", id,
            (link_ok && val) ? "ok" : "absent",
            link_ok ? (val ? ok_stat : absent_stat) : no_link);
    };

    bool_field("qmi",    s_tel.dev_qmi,    "OK", "Absent");
    bool_field("pca",    s_tel.dev_pca,    "OK", "Absent");
    bool_field("ina",    s_tel.dev_ina,    "OK", "Absent");
    bool_field("sd",     s_tel.dev_sd,     "Ready", "No card");
    bool_field("rudder", s_tel.dev_rudder, "OK", "No driver");
    bool_field("winch",  s_tel.dev_winch,  "OK", "No driver");
    bool_field("esc",    s_tel.dev_esc,    "OK", "No driver");

    n += snprintf(buf + n, sizeof(buf) - n,
        ",{\"id\":\"lora\",\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
        link_ok ? "ok" : "absent", link_ok ? "Active" : no_link);

    {
        uint8_t bilge = link_ok ? s_tel.dev_bilge : 0;
        const char *lvl, *stat;
        if (!link_ok)        { lvl = "absent"; stat = no_link; }
        else if (bilge == 2) { lvl = "warn";   stat = "WET"; }
        else if (bilge == 1) { lvl = "ok";     stat = "Dry"; }
        else if (bilge == 3) { lvl = "warn";   stat = "Unverified"; }
        else                 { lvl = "absent"; stat = "Absent"; }
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"bilge\",\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}", lvl, stat);
    }
    {
        bool on = link_ok && s_tel.pump_active;
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"pump\",\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            on ? "warn" : "absent", link_ok ? (on ? "Running" : "Unmonitored") : no_link);
    }
    {
        bool fix = link_ok && s_tel.gps_fix;
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"gps\",\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            fix ? "ok" : "warn", link_ok ? (fix ? "Fix" : "No fix") : no_link);
    }

    n += snprintf(buf + n, sizeof(buf) - n, "]");
    s_srv.send(200, "application/json", buf);
}

static void handle_status() {
    char buf[80];
    snprintf(buf, sizeof(buf),
        "{\"wet\":%s,\"pump\":%s,\"capsized\":%s}",
        s_tel.bilge_wet   ? "true" : "false",
        s_tel.pump_active ? "true" : "false",
        s_tel.capsized    ? "true" : "false");
    s_srv.send(200, "application/json", buf);
}

static void handle_pump() {
    s_pump = s_srv.hasArg("on") && s_srv.arg("on") == "1";
    s_srv.send(200, "text/plain", "OK");
}

static void handle_apple_check() {
    s_srv.send(200, "text/html",
        "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}
static void handle_captive_redirect() {
    s_srv.sendHeader("Location", "http://192.168.5.1/");
    s_srv.send(302, "text/plain", "");
}
static void handle_not_found() { handle_captive_redirect(); }

// ── Mode FSM ──────────────────────────────────────────────────────────────────
enum class XiaoMode { AP, BRIDGE };
static XiaoMode      s_mode       = XiaoMode::AP;
static unsigned long s_last_hb_ms = 0;

static void reset_commanded_state() {
    s_rudder = s_sail = s_throttle = 0.0f;
    s_armed  = false;
    s_pump   = false;
    s_last_cmd_ms = millis();
}

static void start_ap() {
    IPAddress ip(192, 168, 5, 1), gw(192, 168, 5, 1), sn(255, 255, 255, 0);
    WiFi.softAPConfig(ip, gw, sn);
    WiFi.softAP(cfg::AP_SSID, cfg::AP_PASS);
    Serial.printf("bridge: AP up — SSID=%s  IP=%s\n",
                  cfg::AP_SSID, WiFi.softAPIP().toString().c_str());
    s_srv.begin();
}

static void stop_ap() {
    s_srv.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("bridge: AP stopped");
}

static void enter_ap_mode() {
    reset_commanded_state();
    s_pi_len        = 0;
    s_track_len     = 0;
    s_track_last_ms = 0;
    // Return to continuous receive for incoming telemetry.
    s_pkt_ready = false;
    s_radio.startReceive();
    start_ap();
    s_mode = XiaoMode::AP;
    Serial.println("bridge: → Mode 2 (standalone AP)");
}

static void enter_bridge_mode() {
    reset_commanded_state();
    s_pi_len    = 0;
    stop_ap();
    s_pkt_ready = false;
    s_radio.startReceive();
    s_mode = XiaoMode::BRIDGE;
    Serial.println("bridge: → Mode 3 (Pi bridge)");
}

// ── Mode 2: transmit RC frame and poll for telemetry ─────────────────────────
// Called at 50 Hz from the main loop timer.
// Transmits one RC frame then briefly waits for a telemetry response.
static void ap_radio_tick()
{
    // Build and transmit the RC frame.
    uint8_t frame[26];
    build_rc_frame(frame);
    int state = s_radio.transmit(frame, sizeof(frame));
    if (state != RADIOLIB_ERR_NONE)
        Serial.printf("bridge: TX error %d\n", state);

    // Switch to RX and wait a short window for the boat's telemetry reply.
    s_pkt_ready = false;
    s_radio.startReceive();

    unsigned long wait_start = millis();
    while (!s_pkt_ready && (millis() - wait_start) < cfg::TELEM_WAIT_MS)
        ; // busy-wait; short window, OK in this context

    if (s_pkt_ready) {
        s_pkt_ready = false;
        uint8_t rx_buf[64];
        size_t  rx_len = sizeof(rx_buf);
        if (s_radio.readData(rx_buf, rx_len) == RADIOLIB_ERR_NONE)
            process_telem_packet(rx_buf, rx_len);
        // Re-arm continuous receive.
        s_radio.startReceive();
    }
}

// ── Mode 3: frame-aware GFSK bridge ──────────────────────────────────────────
// Pi → GFSK: read CRSF frames from USB-CDC, strip heartbeats, transmit the rest.
// GFSK → Pi: read incoming packets from the radio and forward to Pi via USB-CDC.
static void bridge_pump()
{
    // ── Pi → GFSK ──────────────────────────────────────────────────────────
    while (Serial.available() && s_pi_len < sizeof(s_pi_buf))
        s_pi_buf[s_pi_len++] = (uint8_t)Serial.read();

    while (s_pi_len >= 2) {
        while (s_pi_len > 0 && s_pi_buf[0] != CRSF_SYNC)
            memmove(s_pi_buf, s_pi_buf + 1, --s_pi_len);
        if (s_pi_len < 2) break;
        uint8_t frame_len = s_pi_buf[1];
        size_t  total     = 2u + frame_len;
        if (s_pi_len < total) break;

        if (s_pi_buf[2] == CRSF_TYPE_HEARTBEAT) {
            // Strip heartbeat — update Pi liveness timer but do not forward over GFSK.
            s_last_hb_ms = millis();
        } else {
            // Transmit to boat over GFSK.
            s_radio.transmit(s_pi_buf, total);
            // Brief receive window for boat telemetry response.
            s_pkt_ready = false;
            s_radio.startReceive();
            unsigned long wait_start = millis();
            while (!s_pkt_ready && (millis() - wait_start) < cfg::TELEM_WAIT_MS)
                ;
            if (s_pkt_ready) {
                s_pkt_ready = false;
                uint8_t rx_buf[64];
                size_t  rx_len = sizeof(rx_buf);
                if (s_radio.readData(rx_buf, rx_len) == RADIOLIB_ERR_NONE) {
                    // Forward raw CRSF bytes to Pi so elrs_bridge.py can decode them.
                    Serial.write(rx_buf, rx_len);
                }
                s_radio.startReceive();
            }
        }
        memmove(s_pi_buf, s_pi_buf + total, s_pi_len - total);
        s_pi_len -= total;
    }
}

// ── Arduino entry points ───────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("crsf-bridge firmware starting (SX1262 GFSK)");

    // Initialise hardware SPI and RadioLib.
    SPI.begin(cfg::SX_CLK, cfg::SX_MISO, cfg::SX_MOSI, cfg::SX_CS);
    s_radio.setRfSwitchPins(cfg::SX_RXEN, cfg::SX_TXEN);

    int state = s_radio.beginFSK(
        cfg::FSK_FREQ_MHZ, cfg::FSK_BIT_RATE_KBPS, cfg::FSK_FREQ_DEV_KHZ,
        cfg::FSK_RX_BW_KHZ, cfg::FSK_POWER_DBM, cfg::FSK_PREAMBLE_BITS);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("bridge: SX1262 begin failed (err %d)\n", state);
        // Continue without radio — AP mode still serves the web UI.
    } else {
        s_radio.setDio1Action(on_dio1);
        s_radio.startReceive();
        Serial.printf("bridge: SX1262 ready  %.0f MHz FSK  %.0f kbps  dev=%.0f kHz\n",
                      (double)cfg::FSK_FREQ_MHZ,
                      (double)cfg::FSK_BIT_RATE_KBPS,
                      (double)cfg::FSK_FREQ_DEV_KHZ);
    }

    // Register web server routes once — they persist across stop/begin cycles.
    s_srv.on("/",            handle_root);
    s_srv.on("/map",         handle_map);
    s_srv.on("/control",     handle_control);
    s_srv.on("/telemetry",   handle_telemetry);
    s_srv.on("/status",      handle_status);
    s_srv.on("/diag.json",   handle_diag);
    s_srv.on("/pump",        handle_pump);
    s_srv.on("/track",       handle_track);
    s_srv.on("/hotspot-detect.html",       handle_apple_check);
    s_srv.on("/library/test/success.html", handle_apple_check);
    s_srv.on("/hotspotdetect.html",        handle_apple_check);
    s_srv.on("/generate_204",  []() { s_srv.send(204, "text/plain", ""); });
    s_srv.on("/ncsi.txt",      handle_captive_redirect);
    s_srv.on("/connecttest.txt", handle_captive_redirect);
    s_srv.onNotFound(handle_not_found);

    // Boot in Mode 2.  Pretend Pi last heartbeat was 3 s ago so we start
    // past HB_TIMEOUT_MS and stay in AP mode until a real Pi connects.
    s_last_hb_ms = millis() - cfg::HB_TIMEOUT_MS - 1000;
    enter_ap_mode();

    Serial.println("crsf-bridge ready");
}

void loop()
{
    unsigned long now = millis();

    if (s_mode == XiaoMode::AP) {
        // ── Mode 2: standalone AP ─────────────────────────────────────────

        // Check Pi USB serial for heartbeat → switch to Mode 3.
        while (Serial.available() && s_pi_len < sizeof(s_pi_buf))
            s_pi_buf[s_pi_len++] = (uint8_t)Serial.read();
        if (scan_pi_for_heartbeat()) {
            s_last_hb_ms = now;
            Serial.println("bridge: Pi heartbeat detected — switching to Mode 3");
            enter_bridge_mode();
            return;
        }

        // Serve HTTP.
        s_srv.handleClient();

        // Transmit RC frame at 50 Hz and poll for telemetry reply.
        static unsigned long s_tx_ms = 0;
        if (now - s_tx_ms >= cfg::TX_PERIOD_MS) {
            s_tx_ms = now;
            ap_radio_tick();
        }

        // Record GPS track point every 5 s when fix valid.
        if (s_tel.gps_fix && (now - s_track_last_ms) >= 5000) {
            s_track_last_ms = now;
            if (s_track_len < 200)
                s_track[s_track_len++] = { (float)s_tel.gps_lat, (float)s_tel.gps_lng };
        }

    } else {
        // ── Mode 3: Pi bridge ─────────────────────────────────────────────
        bridge_pump();

        // Revert to Mode 2 when Pi heartbeat times out.
        if (now - s_last_hb_ms >= cfg::HB_TIMEOUT_MS) {
            Serial.println("bridge: Pi heartbeat lost — switching to Mode 2 (AP)");
            enter_ap_mode();
        }
    }
}
