// main.cpp — XIAO ESP32-S3 dual-mode CRSF bridge firmware.
//
// Two operating modes, switched dynamically based on Pi heartbeat detection:
//
//   Mode 2 — Standalone AP  (default at boot, Pi absent)
//     • Brings up WiFi AP "Mistral-2" and serves the shared embedded control page.
//     • Generates CRSF RC Channels Packed (0x16) to the Ranger Micro at 50 Hz.
//     • Decodes incoming CRSF telemetry from the Ranger Micro and exposes it via
//       HTTP JSON endpoints — same API surface as the boat's own wifi_ctrl.cpp.
//     • Control timeout: if no /control hit for 500 ms, output goes to neutral/disarmed.
//
//   Mode 3 — Pi bridge  (active while Pi's elrs_bridge.py heartbeat is detected)
//     • Tears down the AP; becomes a transparent USB-CDC ↔ UART1 byte pump.
//     • Strips Pi heartbeat frames (type 0x7E) before forwarding to the Ranger Micro.
//     • The Pi's elrs_bridge.py emits 0x7E heartbeats every ~500 ms as a liveness
//       signal.  2 s silence → revert to Mode 2.
//
//   Transition safety: on every mode switch, commanded state resets to
//   neutral/disarmed before the new source takes over.
//
// Wiring (XIAO ESP32-S3):
//   Serial1 TX  = GPIO43 (D6) → 470Ω resistor → Ranger Micro signal pin
//   Serial1 RX  = GPIO44 (D7) direct
//   USB-CDC     = Serial (native USB, enumerates as /dev/ttyACM0 on Pi)
//
// Shared UI headers (from shared/web/ via -I"${PROJECT_DIR}/../shared"):
//   web/control_page.h — HTML_PAGE[] PROGMEM (shared with boat's wifi_ctrl.cpp)
//   web/map_page.h     — MAP_HTML[] PROGMEM

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <string.h>

#include "web/control_page.h"   // HTML_PAGE[] PROGMEM — shared control page
#include "web/map_page.h"       // MAP_HTML[] PROGMEM  — shared track viewer

// ── Configuration ──────────────────────────────────────────────────────────────
namespace cfg {
    constexpr int    CRSF_RX           = 44;      // D7, direct from Ranger Micro
    constexpr int    CRSF_TX           = 43;      // D6 → 470Ω → Ranger Micro signal pin
    constexpr uint32_t CRSF_BAUD      = 420000;  // ELRS CRSF standard (same as receiver-to-FC)
    constexpr char   AP_SSID[]         = "Mistral-2";
    constexpr char   AP_PASS[]         = "readyabout";
    constexpr uint32_t CTRL_TIMEOUT_MS = 500;    // servo timeout (same as boat)
    constexpr uint32_t MODE_REQUEST_TIMEOUT_MS = 30000; // CH_MODE presence signal —
                                                          // 30 s of inactivity before dropping
                                                          // to centre, so Mode 2 sessions stay
                                                          // stable across browser polling gaps
    constexpr uint32_t TX_PERIOD_MS    = 20;     // 50 Hz RC output
    constexpr uint32_t HB_TIMEOUT_MS   = 2000;  // Pi absence → revert to AP mode
}

// ── CRSF frame types and channel limits ───────────────────────────────────────
constexpr uint8_t  CRSF_SYNC            = 0xC8;
constexpr uint8_t  CRSF_TYPE_RC         = 0x16; // RC Channels Packed (Pi/XIAO → TX)
constexpr uint8_t  CRSF_TYPE_GPS        = 0x02; // GPS telemetry (boat → base)
constexpr uint8_t  CRSF_TYPE_BATTERY    = 0x08; // Battery sensor
constexpr uint8_t  CRSF_TYPE_LINK_STATS = 0x14; // Link statistics
constexpr uint8_t  CRSF_TYPE_ATTITUDE   = 0x1E; // Attitude
constexpr uint8_t  CRSF_TYPE_SAILBOAT   = 0x80; // Custom sailboat frame
constexpr uint8_t  CRSF_TYPE_DEVICES    = 0x81; // Custom device-status bitmap
constexpr uint8_t  CRSF_TYPE_HEARTBEAT  = 0x7E; // Pi liveness signal (stripped, not forwarded)
constexpr uint8_t  CRSF_TYPE_RADIO_ID   = 0x3A; // Timing sync from TX module (JR bay)

constexpr uint16_t CRSF_CH_MIN    = 172;
constexpr uint16_t CRSF_CH_CENTER = 992;
constexpr uint16_t CRSF_CH_MAX    = 1811;

// Channel indices — 0-based, matching boat-firmware/src/protocol.h and shared/protocol.py
constexpr int CH_RUDDER   = 0;
constexpr int CH_SAIL     = 1;
constexpr int CH_THROTTLE = 2;
constexpr int CH_ARM      = 3;
constexpr int CH_MODE     = 4;
constexpr int CH_RESTART  = 5;
constexpr int CH_PUMP     = 6;

// ── File-scope 50 Hz TX timer (shared with parse_radio_stream for sync-aligned sends) ──
static unsigned long s_tx_ms = 0;

// ── CRC-8/DVB-S2 (polynomial 0xD5) — identical to telemetry.cpp ──────────────
static uint8_t crsf_crc8(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1);
    }
    return crc;
}

// ── RC frame builder ──────────────────────────────────────────────────────────

// Map –1.0..+1.0 → CRSF_CH_MIN..CRSF_CH_MAX, centered at 992.
static uint16_t ch_centered(float v) {
    int val = (int)roundf(v * (float)(CRSF_CH_MAX - CRSF_CH_CENTER) + CRSF_CH_CENTER);
    if (val < CRSF_CH_MIN) val = CRSF_CH_MIN;
    if (val > CRSF_CH_MAX) val = CRSF_CH_MAX;
    return (uint16_t)val;
}
// Map 0.0..+1.0 → CRSF_CH_MIN..CRSF_CH_MAX.
static uint16_t ch_unipolar(float v) {
    int val = (int)roundf(v * (float)(CRSF_CH_MAX - CRSF_CH_MIN) + CRSF_CH_MIN);
    if (val < CRSF_CH_MIN) val = CRSF_CH_MIN;
    if (val > CRSF_CH_MAX) val = CRSF_CH_MAX;
    return (uint16_t)val;
}

// Pack 16 × 11-bit channel values LSB-first into out[22].
// Mirrors the Python expression `bits.to_bytes(22, "little")` in elrs_bridge.py.
static void pack_channels(const uint16_t ch[16], uint8_t out[22]) {
    memset(out, 0, 22);
    for (int i = 0; i < 16; i++) {
        int      bit_pos  = i * 11;
        int      byte_idx = bit_pos >> 3;
        int      bit_off  = bit_pos & 7;
        uint16_t val      = ch[i] & 0x7FF;
        out[byte_idx]     |= (uint8_t)((val << bit_off) & 0xFF);
        out[byte_idx + 1] |= (uint8_t)((val >> (8 - bit_off)) & 0xFF);
        if (bit_off > 5)   // 11 bits spans a third byte when offset > 5
            out[byte_idx + 2] |= (uint8_t)((val >> (16 - bit_off)) & 0xFF);
    }
}

// ── Commanded control state (Mode 2; reset on every mode switch) ──────────────
static float         s_rudder      = 0.0f;
static float         s_sail        = 0.0f;
static float         s_throttle    = 0.0f;
static bool          s_armed       = false;
static bool          s_pump        = false;
static unsigned long s_last_cmd_ms = 0;

// CRSF DEVICE_PING — announces the XIAO as a handset to the Ranger Micro.
// EdgeTX sends this once per second; without it some ELRS TX module firmware
// stays in "no handset" mode and ignores incoming RC channels.
// CRSF format (CRSF WG spec): [dest][len=3][0x28][origin][CRC over type+origin]
//   dest=0xEE  (CRSF transmitter = Ranger Micro)
//   origin=0xEA (radio transmitter = XIAO/handset)
static void send_device_ping() {
    uint8_t f[5];
    f[0] = 0xEE;                  // destination: CRSF transmitter (Ranger Micro)
    f[1] = 3;                     // len = type(1) + origin(1) + CRC(1)
    f[2] = 0x28;                  // DEVICE_PING
    f[3] = 0xEA;                  // origin: radio transmitter (handset/XIAO)
    f[4] = crsf_crc8(f + 2, 2);  // CRC over type + origin
    Serial1.write(f, sizeof(f));
}

// Build a 26-byte CRSF RC Channels Packed frame from current commanded state.
// Applies control timeout: if no /control hit recently, output neutral/disarmed.
static void build_rc_frame(uint8_t buf[26]) {
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = CRSF_CH_CENTER;

    bool timed_out = (millis() - s_last_cmd_ms) > cfg::CTRL_TIMEOUT_MS;
    bool active    = s_armed && !timed_out;

    ch[CH_RUDDER]   = ch_centered(active ? s_rudder : 0.0f);
    ch[CH_SAIL]     = ch_unipolar(s_sail);
    ch[CH_THROTTLE] = ch_centered(active ? s_throttle : 0.0f);
    ch[CH_ARM]      = active ? CRSF_CH_MAX : CRSF_CH_MIN;
    // Request the boat switch into ELRS mode only while someone actually has
    // the Mode 2 page open and polling /control -- not merely because the
    // XIAO+Ranger Micro happen to be powered on with no one connected.
    // Independent of `active`/armed: switching into ELRS mode to check
    // telemetry before arming is intentionally allowed. Uses the looser
    // MODE_REQUEST_TIMEOUT_MS (not the 500 ms servo `timed_out`) so brief
    // polling gaps don't flap the boat's WiFi AP up/down. See
    // docs/protocol.md "Remote mode switching".
    bool mode_timed_out = (millis() - s_last_cmd_ms) > cfg::MODE_REQUEST_TIMEOUT_MS;
    ch[CH_MODE]     = mode_timed_out ? CRSF_CH_CENTER : CRSF_CH_MAX;
    ch[CH_RESTART]  = CRSF_CH_MIN;      // idle
    ch[CH_PUMP]     = s_pump ? CRSF_CH_MAX : CRSF_CH_MIN;  // not arm-gated

    uint8_t payload[22];
    pack_channels(ch, payload);

    // Frame: [0xEE][24][0x16][22-byte payload][CRC]
    // Destination 0xEE = CRSF_ADDRESS_CRSF_TRANSMITTER (Ranger Micro).
    // Handset→TX module frames must use 0xEE, not 0xC8 (FC address); using
    // 0xC8 causes some ELRS TX firmware to ignore the frame for handset detection.
    // length field = 24 = type(1) + payload(22) + CRC(1)
    buf[0] = 0xEE;
    buf[1] = 24;
    buf[2] = CRSF_TYPE_RC;
    memcpy(buf + 3, payload, 22);
    buf[25] = crsf_crc8(buf + 2, 23);  // CRC covers type + payload (23 bytes)
}

// ── Telemetry state ────────────────────────────────────────────────────────────
// Populated in Mode 2 by decoding incoming CRSF frames from the Ranger Micro.
struct TelState {
    // Battery (CRSF 0x08) — 2 Hz
    float  voltage_v    = 0.0f;
    float  current_a    = 0.0f;
    float  mah_used     = 0.0f;
    int    battery_pct  = 0;
    // Attitude (CRSF 0x1E) — 24 Hz
    float  pitch_deg    = 0.0f;
    float  roll_deg     = 0.0f;
    float  yaw_deg      = 0.0f;
    // Sailboat custom (CRSF 0x80) — 9-byte payload (protocol v2), 5 Hz
    float  rud_cmd      = 0.0f;
    float  sail_cmd     = 0.0f;
    float  throttle_cmd = 0.0f;
    float  mcu_temp_c   = 0.0f;
    bool   capsized     = false;
    bool   bilge_wet    = false;
    bool   pump_active  = false;
    bool   boat_armed   = false;
    bool   failsafe     = false;
    // Link stats (CRSF 0x14)
    int    rssi_up      = 0;
    int    lq_up        = 0;
    // Device status bitmap (CRSF 0x81) — 0.2 Hz; 2-bit fields, mirrors boat-firmware/src/telemetry.cpp
    uint8_t dev_qmi      = 0, dev_pca = 0, dev_ina = 0, dev_sd = 0;
    uint8_t dev_bilge    = 0, dev_rudder = 0, dev_winch = 0, dev_esc = 0;
    // GPS (CRSF 0x02) — 1 Hz when fix
    double gps_lat      = 0.0;
    double gps_lng      = 0.0;
    float  gps_alt_m    = 0.0f;
    float  gps_spd_kn   = 0.0f;
    float  gps_hdg_deg  = 0.0f;
    int    gps_sats     = 0;
    bool   gps_fix      = false;
};
static TelState s_tel;

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

static void decode_attitude(const uint8_t *p, size_t len) {
    if (len < 6) return;
    // int16 × 10000 radians → degrees
    s_tel.pitch_deg = get_i16be(p + 0) / 10000.0f * (180.0f / (float)M_PI);
    s_tel.roll_deg  = get_i16be(p + 2) / 10000.0f * (180.0f / (float)M_PI);
    s_tel.yaw_deg   = get_i16be(p + 4) / 10000.0f * (180.0f / (float)M_PI);
}

static void decode_sailboat(const uint8_t *p, size_t len) {
    if (len < 9) return;   // protocol v2: 9-byte payload includes status byte
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

// Bit layout (LSB-first, 3 bytes): [1:0]ft [3:2]qmi [5:4]pca [7:6]ina [9:8]sd
// [11:10]bilge_sensor [13:12]bilge_pump [15:14]rudder [17:16]winch [19:18]esc.
// Mirrors boat-firmware/src/telemetry.cpp send_devices(). ft and bilge_pump
// (always 0 on the boat) are not surfaced — pump state comes from the
// sailboat status byte instead, which is fresher (5 Hz vs 0.2 Hz).
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
    // int32 lat/lng × 1e7; uint16 speed km/h×10, heading °×100, altitude m+1000; uint8 sats
    int32_t  lat_raw = ((int32_t)p[0]<<24)|((int32_t)p[1]<<16)|((int32_t)p[2]<<8)|p[3];
    int32_t  lng_raw = ((int32_t)p[4]<<24)|((int32_t)p[5]<<16)|((int32_t)p[6]<<8)|p[7];
    uint16_t spd_raw = ((uint16_t)p[8] <<8)|p[9];
    uint16_t hdg_raw = ((uint16_t)p[10]<<8)|p[11];
    uint16_t alt_raw = ((uint16_t)p[12]<<8)|p[13];
    s_tel.gps_lat    = lat_raw / 1e7;
    s_tel.gps_lng    = lng_raw / 1e7;
    s_tel.gps_spd_kn = spd_raw / 10.0f / 1.852f;  // CRSF GPS frame uses km/h × 10
    s_tel.gps_hdg_deg= hdg_raw / 100.0f;
    s_tel.gps_alt_m  = (float)alt_raw - 1000.0f;
    s_tel.gps_sats   = p[14];
    s_tel.gps_fix    = (p[14] >= 3);
}

// ── Streaming CRSF buffers ─────────────────────────────────────────────────────
// Two separate byte buffers — one per serial direction.

// Serial1 (Ranger Micro → XIAO): incoming CRSF telemetry in Mode 2.
static uint8_t s_radio_buf[512];
static size_t  s_radio_len = 0;

// Serial (Pi USB → XIAO): Pi's RC frames + heartbeats.
static uint8_t s_pi_buf[256];
static size_t  s_pi_len = 0;

// millis() of the last validly-CRC'd frame from the Ranger Micro — used by
// /diag.json to tell "boat reachable over RF" from "boat link is down",
// instead of showing stale or default-true device status.
static unsigned long s_radio_last_ms = 0;

// Consume all complete, valid CRSF frames from the radio (Serial1) buffer.
// Decodes telemetry into s_tel.
static void parse_radio_stream() {
    static uint32_t s_dbg_crc = 0;
    // Track non-0xC8 bytes dropped during sync-seeking — if non-zero,
    // the Ranger Micro sends frames with a different address byte that
    // our parser currently discards (e.g. 0xEA = radio-transmitter-directed).
    static uint32_t s_dbg_skip = 0;
    static uint8_t  s_skip_seen[8] = {};
    static uint8_t  s_skip_n = 0;
    while (s_radio_len >= 2) {
        // Seek sync — only 0xC8 is a valid frame start for our parser.
        // Track any non-0xC8 bytes that get skipped; if 0xEA or 0xEE appear
        // here, the Ranger Micro sends TX→handset frames we're not yet parsing.
        while (s_radio_len > 0 && s_radio_buf[0] != CRSF_SYNC) {
            uint8_t b = s_radio_buf[0];
            bool found = false;
            for (uint8_t i = 0; i < s_skip_n; i++) if (s_skip_seen[i]==b) { found=true; break; }
            if (!found && s_skip_n < 8) s_skip_seen[s_skip_n++] = b;
            s_dbg_skip++;
            memmove(s_radio_buf, s_radio_buf + 1, --s_radio_len);
        }
        if (s_radio_len < 2) break;
        uint8_t frame_len = s_radio_buf[1];
        size_t  total     = 2u + frame_len;
        if (s_radio_len < total) break;  // wait for full frame
        if (crsf_crc8(s_radio_buf + 2, frame_len - 1) != s_radio_buf[total - 1]) {
            s_dbg_crc++;
            memmove(s_radio_buf, s_radio_buf + 1, --s_radio_len);
            continue;
        }
        s_radio_last_ms = millis();
        uint8_t        type    = s_radio_buf[2];
        const uint8_t *payload = s_radio_buf + 3;
        size_t         plen    = (size_t)(frame_len - 2);
        static uint32_t dbg_att=0, dbg_bat=0, dbg_gps=0, dbg_sb=0, dbg_lq=0;
        // Track unknown types in a small histogram (first 8 distinct types seen)
        static uint8_t  dbg_unk_types[8] = {};
        static uint32_t dbg_unk_count[8] = {};
        static uint8_t  dbg_unk_n = 0;
        switch (type) {
            case CRSF_TYPE_GPS:        decode_gps(payload, plen);        dbg_gps++; break;
            case CRSF_TYPE_BATTERY:    decode_battery(payload, plen);    dbg_bat++; break;
            case CRSF_TYPE_ATTITUDE:   decode_attitude(payload, plen);   dbg_att++; break;
            case CRSF_TYPE_SAILBOAT:   decode_sailboat(payload, plen);   dbg_sb++;  break;
            case CRSF_TYPE_LINK_STATS: decode_link_stats(payload, plen); dbg_lq++;  break;
            case CRSF_TYPE_DEVICES:    decode_devices(payload, plen);                break; // 0x81 — boat device status
            case CRSF_TYPE_RADIO_ID:                                                 break; // 0x3A — timing sync; no active response needed
            default: {
                bool found = false;
                for (uint8_t i = 0; i < dbg_unk_n; i++) {
                    if (dbg_unk_types[i] == type) { dbg_unk_count[i]++; found = true; break; }
                }
                if (!found && dbg_unk_n < 8) { dbg_unk_types[dbg_unk_n] = type; dbg_unk_count[dbg_unk_n++] = 1; }
                break;
            }
        }
        static unsigned long dbg_last_ms = 0;
        unsigned long dbg_now = millis();
        if (dbg_now - dbg_last_ms >= 5000) {
            dbg_last_ms = dbg_now;
            Serial.printf("[telem/5s] att=%u bat=%u gps=%u sb=%u lq=%u crc_fail=%u skip=%u | roll=%.1f pit=%.1f yaw=%.1f\n",
                dbg_att, dbg_bat, dbg_gps, dbg_sb, dbg_lq, s_dbg_crc, s_dbg_skip,
                s_tel.roll_deg, s_tel.pitch_deg, s_tel.yaw_deg);
            if (s_skip_n > 0) {
                Serial.print("[skip] non-sync bytes: ");
                for (uint8_t i = 0; i < s_skip_n; i++)
                    Serial.printf("0x%02X ", s_skip_seen[i]);
                Serial.println();
            }
            for (uint8_t i = 0; i < dbg_unk_n; i++)
                Serial.printf("[unk] type=0x%02X count=%u\n", dbg_unk_types[i], dbg_unk_count[i]);
        }
        memmove(s_radio_buf, s_radio_buf + total, s_radio_len - total);
        s_radio_len -= total;
    }
}

// Scan the Pi USB buffer for heartbeat frames (Mode 2 entry trigger).
// Discards all frames — nothing is forwarded to the Ranger Micro in AP mode.
// Returns true if at least one heartbeat (0x7E) frame was found.
static bool scan_pi_for_heartbeat() {
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

// ── GPS track buffer (Mode 2) ─────────────────────────────────────────────────
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

// Reports real device status relayed from the boat's CRSF DEVICES (0x81) frame,
// instead of leaving the page's hard-coded "OK" defaults unanswered (the page
// only overwrites them on a successful /diag.json fetch — see control_page.h
// updateDevices()). Gated on s_radio_last_ms so a dead RF link shows "No RF
// link" rather than stale or default-true status.
static void handle_diag() {
    char buf[768];
    int  n = 0;
    n += snprintf(buf, sizeof(buf), "[");

    bool link_ok = s_radio_last_ms != 0 && (millis() - s_radio_last_ms) < 5000;
    const char *no_link = "No RF link";

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
        ",{\"id\":\"elrs\",\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
        link_ok ? "ok" : "absent", link_ok ? "Active" : no_link);

    {
        uint8_t bilge = link_ok ? s_tel.dev_bilge : 0;
        const char *lvl, *stat;
        if (!link_ok)        { lvl = "absent"; stat = no_link; }
        else if (bilge == 2) { lvl = "warn";    stat = "WET"; }
        else if (bilge == 1) { lvl = "ok";      stat = "Dry"; }
        else if (bilge == 3) { lvl = "warn";    stat = "Unverified"; }
        else                 { lvl = "absent";  stat = "Absent"; }
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
static unsigned long s_last_hb_ms = 0;  // millis() of last Pi heartbeat frame

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
    s_radio_len   = 0;
    s_pi_len      = 0;
    s_track_len   = 0;
    s_track_last_ms = 0;
    start_ap();
    s_mode = XiaoMode::AP;
    Serial.println("bridge: → Mode 2 (standalone AP)");
}

static void enter_bridge_mode() {
    reset_commanded_state();
    s_radio_len = 0;
    s_pi_len    = 0;
    stop_ap();
    s_mode = XiaoMode::BRIDGE;
    Serial.println("bridge: → Mode 3 (Pi bridge)");
}

// ── Mode 3: frame-aware byte pump ─────────────────────────────────────────────
// Forwards Pi→Ranger Micro while stripping heartbeat frames (0x7E) so the TX
// module never sees them.  Ranger Micro→Pi is passed through verbatim (telemetry).

static void bridge_pump() {
    // Pi → Ranger Micro: buffer, strip heartbeats, forward all other frames.
    while (Serial.available() && s_pi_len < sizeof(s_pi_buf))
        s_pi_buf[s_pi_len++] = (uint8_t)Serial.read();

    while (s_pi_len >= 2) {
        while (s_pi_len > 0 && s_pi_buf[0] != CRSF_SYNC)
            memmove(s_pi_buf, s_pi_buf + 1, --s_pi_len);
        if (s_pi_len < 2) break;
        uint8_t frame_len = s_pi_buf[1];
        size_t  total     = 2u + frame_len;
        if (s_pi_len < total) break;  // wait for full frame

        if (s_pi_buf[2] == CRSF_TYPE_HEARTBEAT) {
            // Strip: update liveness timestamp, do not forward
            s_last_hb_ms = millis();
        } else {
            // Forward complete frame to Ranger Micro verbatim
            Serial1.write(s_pi_buf, total);
        }
        memmove(s_pi_buf, s_pi_buf + total, s_pi_len - total);
        s_pi_len -= total;
    }

    // Ranger Micro → Pi: transparent (link stats, CRSF telemetry echoed to Pi)
    while (Serial1.available())
        Serial.write(Serial1.read());
}

// ── Arduino entry points ───────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("crsf-bridge firmware starting");

    Serial1.begin(cfg::CRSF_BAUD, SERIAL_8N1, cfg::CRSF_RX, cfg::CRSF_TX);

    // Register web server handlers once — they persist across stop/begin cycles.
    s_srv.on("/",            handle_root);
    s_srv.on("/map",         handle_map);
    s_srv.on("/control",     handle_control);
    s_srv.on("/telemetry",   handle_telemetry);
    s_srv.on("/status",      handle_status);
    s_srv.on("/diag.json",   handle_diag);
    s_srv.on("/pump",        handle_pump);
    s_srv.on("/track",       handle_track);
    // Captive portal (iOS, Android, Windows connectivity probes)
    s_srv.on("/hotspot-detect.html",        handle_apple_check);
    s_srv.on("/library/test/success.html",  handle_apple_check);
    s_srv.on("/hotspotdetect.html",         handle_apple_check);
    s_srv.on("/generate_204",  []() { s_srv.send(204, "text/plain", ""); });
    s_srv.on("/ncsi.txt",      handle_captive_redirect);
    s_srv.on("/connecttest.txt", handle_captive_redirect);
    s_srv.onNotFound(handle_not_found);

    // Boot in Mode 2 (standalone AP).  Pretend the Pi last heartbeat was 3 s ago
    // so we start past HB_TIMEOUT_MS and stay in AP mode until a Pi connects.
    s_last_hb_ms = millis() - cfg::HB_TIMEOUT_MS - 1000;
    enter_ap_mode();

    Serial.println("crsf-bridge ready");
}

void loop() {
    unsigned long now = millis();

    if (s_mode == XiaoMode::AP) {
        // ── Drain radio (Ranger Micro → XIAO) telemetry ───────────────────
        while (Serial1.available() && s_radio_len < sizeof(s_radio_buf))
            s_radio_buf[s_radio_len++] = (uint8_t)Serial1.read();
        parse_radio_stream();

        // ── Record a GPS track point every 5 s when fix ───────────────────
        if (s_tel.gps_fix && (now - s_track_last_ms) >= 5000) {
            s_track_last_ms = now;
            if (s_track_len < 200)
                s_track[s_track_len++] = { (float)s_tel.gps_lat, (float)s_tel.gps_lng };
        }

        // ── Check Pi USB serial for heartbeat ─────────────────────────────
        while (Serial.available() && s_pi_len < sizeof(s_pi_buf))
            s_pi_buf[s_pi_len++] = (uint8_t)Serial.read();
        if (scan_pi_for_heartbeat()) {
            s_last_hb_ms = now;
            Serial.println("bridge: Pi heartbeat detected — switching to Mode 3");
            enter_bridge_mode();
            return;
        }

        // ── Serve web requests ─────────────────────────────────────────────
        s_srv.handleClient();

        // ── Emit RC frame at 50 Hz ─────────────────────────────────────────
        if (now - s_tx_ms >= cfg::TX_PERIOD_MS) {
            s_tx_ms = now;
            uint8_t frame[26];
            build_rc_frame(frame);
            Serial1.write(frame, sizeof(frame));
        }

        // ── DEVICE_PING at 1 Hz — required by ELRS TX modules to leave ────
        // "no handset" mode (mirrors what EdgeTX sends to announce a radio).
        static unsigned long s_ping_ms = 0;
        if (now - s_ping_ms >= 1000) {
            s_ping_ms = now;
            send_device_ping();
        }

    } else {
        // ── Mode 3: Pi bridge ─────────────────────────────────────────────
        bridge_pump();

        // ── Heartbeat timeout → revert to AP ──────────────────────────────
        if (now - s_last_hb_ms >= cfg::HB_TIMEOUT_MS) {
            Serial.println("bridge: Pi heartbeat lost — switching to Mode 2 (AP)");
            enter_ap_mode();
        }
    }
}
