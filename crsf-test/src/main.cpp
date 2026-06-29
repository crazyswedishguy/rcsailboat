// crsf-test — minimal CRSF diagnostic firmware for XIAO ESP32-S3
//
// Purpose: diagnose why the Ranger Micro stays in "no handset" mode.
// No WiFi, no web server — just UART + USB serial logging.
//
// What it does every loop:
//   • Drains Serial1 and parses all incoming bytes as CRSF frames,
//     accepting any valid CRSF address byte (0xC8, 0xEA, 0xEE, 0xEC, …).
//   • Prints every non-echo frame with address, type, length, and hex payload.
//   • Sends RC Channels Packed (0x16) at 50 Hz, destination 0xEE.
//   • Sends DEVICE_PING (0x28) at 1 Hz, destination 0xEE.
//   • Prints a summary every 5 s: counts per (addr, type) pair.
//
// Echo detection: frames with addr=0xEE are our own TX echoed back on the
// half-duplex JR-bay bus — they are counted but not printed verbosely.
//
// What to look for:
//   addr=0xEA  — Ranger Micro→handset frames (link stats, DEVICE_INFO)
//   type=0x29  — DEVICE_INFO: Ranger Micro responded to our DEVICE_PING
//   type=0x3A  — RADIO_ID: Ranger Micro timing sync (printed for inspection)
//   Ranger Micro LED change to solid green/blue = handset recognized

#include <Arduino.h>
#include <string.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
constexpr int      CRSF_RX    = 44;       // GPIO44 (D7), direct from Ranger Micro
constexpr int      CRSF_TX    = 43;       // GPIO43 (D6), → 470 Ω → Ranger Micro
constexpr uint32_t CRSF_BAUD  = 420000;

// ── CRSF address bytes ────────────────────────────────────────────────────────
constexpr uint8_t ADDR_BROADCAST  = 0x00;
constexpr uint8_t ADDR_FC         = 0xC8;  // flight controller
constexpr uint8_t ADDR_HANDSET    = 0xEA;  // radio transmitter (us, pretending to be handset)
constexpr uint8_t ADDR_RX         = 0xEC;  // CRSF receiver
constexpr uint8_t ADDR_TX_MODULE  = 0xEE;  // CRSF transmitter (Ranger Micro)

// All address bytes we'll accept as potential frame starts.
static bool is_valid_crsf_addr(uint8_t b) {
    return b == ADDR_BROADCAST || b == ADDR_FC    || b == ADDR_HANDSET ||
           b == ADDR_RX        || b == ADDR_TX_MODULE ||
           b == 0xC0 || b == 0xC2 || b == 0xC4 || b == 0xCC;  // TBS/GPS/misc
}

// ── CRC-8/DVB-S2 ─────────────────────────────────────────────────────────────
static uint8_t crsf_crc8(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1);
    }
    return crc;
}

// ── Channel packing ───────────────────────────────────────────────────────────
static void pack_channels(const uint16_t ch[16], uint8_t out[22]) {
    memset(out, 0, 22);
    for (int i = 0; i < 16; i++) {
        int bit_pos  = i * 11;
        int byte_idx = bit_pos >> 3;
        int bit_off  = bit_pos & 7;
        uint16_t val = ch[i] & 0x7FF;
        out[byte_idx]     |= (uint8_t)((val << bit_off) & 0xFF);
        out[byte_idx + 1] |= (uint8_t)((val >> (8 - bit_off)) & 0xFF);
        if (bit_off > 5)
            out[byte_idx + 2] |= (uint8_t)((val >> (16 - bit_off)) & 0xFF);
    }
}

// ── Frame senders ─────────────────────────────────────────────────────────────

static void send_device_ping() {
    uint8_t f[5] = { ADDR_TX_MODULE, 3, 0x28, ADDR_HANDSET, 0 };
    f[4] = crsf_crc8(f + 2, 2);
    Serial1.write(f, 5);
    Serial.printf("[TX] DEVICE_PING → 0xEE  (crc=0x%02X)\n", f[4]);
}

static void send_rc_frame(uint16_t *ch_override = nullptr) {
    uint16_t ch[16];
    if (ch_override) {
        memcpy(ch, ch_override, 16 * sizeof(uint16_t));
    } else {
        for (int i = 0; i < 16; i++) ch[i] = 992;  // all centered
    }
    uint8_t payload[22];
    pack_channels(ch, payload);
    uint8_t buf[26];
    buf[0] = ADDR_TX_MODULE;  // 0xEE
    buf[1] = 24;
    buf[2] = 0x16;
    memcpy(buf + 3, payload, 22);
    buf[25] = crsf_crc8(buf + 2, 23);
    Serial1.write(buf, 26);
}

// ── Frame statistics ──────────────────────────────────────────────────────────

struct FrameStat {
    uint8_t  addr, type;
    uint32_t count;
};
static FrameStat s_stats[24];
static uint8_t   s_nstats = 0;
static uint32_t  s_crc_fail  = 0;
static uint32_t  s_skip_byte = 0;

static void record(uint8_t addr, uint8_t type) {
    for (uint8_t i = 0; i < s_nstats; i++) {
        if (s_stats[i].addr == addr && s_stats[i].type == type) {
            s_stats[i].count++;
            return;
        }
    }
    if (s_nstats < 24) s_stats[s_nstats++] = { addr, type, 1 };
}

static void print_summary(unsigned long elapsed_ms) {
    Serial.printf("\n── %lu ms summary | crc_fail=%u  skipped=%u ──\n",
        elapsed_ms, s_crc_fail, s_skip_byte);
    for (uint8_t i = 0; i < s_nstats; i++) {
        const char *aname;
        switch (s_stats[i].addr) {
            case ADDR_FC:        aname = "FC";     break;
            case ADDR_HANDSET:   aname = "HNDST";  break;
            case ADDR_TX_MODULE: aname = "TX(ours)"; break;
            case ADDR_RX:        aname = "RX";     break;
            default:             aname = "?";      break;
        }
        Serial.printf("  [%s/0x%02X] type=0x%02X  count=%u\n",
            aname, s_stats[i].addr, s_stats[i].type, s_stats[i].count);
        s_stats[i].count = 0;
    }
    Serial.println("────────────────────────────────────────────");
    s_crc_fail  = 0;
    s_skip_byte = 0;
}

// ── Stream parser ─────────────────────────────────────────────────────────────

static uint8_t s_buf[512];
static size_t  s_len = 0;

static void parse_stream() {
    while (s_len >= 4) {
        if (!is_valid_crsf_addr(s_buf[0])) {
            s_skip_byte++;
            memmove(s_buf, s_buf + 1, --s_len);
            continue;
        }

        uint8_t frame_len = s_buf[1];
        // Sanity-check length: CRSF frames are 2..64 bytes of content.
        if (frame_len < 2 || frame_len > 62) {
            s_skip_byte++;
            memmove(s_buf, s_buf + 1, --s_len);
            continue;
        }

        size_t total = 2u + frame_len;
        if (s_len < total) break;

        uint8_t expected = crsf_crc8(s_buf + 2, frame_len - 1);
        if (expected != s_buf[total - 1]) {
            s_crc_fail++;
            memmove(s_buf, s_buf + 1, --s_len);
            continue;
        }

        uint8_t addr = s_buf[0];
        uint8_t type = s_buf[2];
        record(addr, type);

        // Suppress verbose output for our own echoes (addr=0xEE, type=0x16 or 0x28).
        bool our_echo = (addr == ADDR_TX_MODULE && (type == 0x16 || type == 0x28));
        if (!our_echo) {
            const char *aname;
            switch (addr) {
                case ADDR_FC:        aname = "FC";    break;
                case ADDR_HANDSET:   aname = "HNDST"; break;
                case ADDR_TX_MODULE: aname = "TX";    break;
                case ADDR_RX:        aname = "RX";    break;
                default:             aname = "?";     break;
            }
            size_t plen = (size_t)(frame_len - 2);
            Serial.printf("[RX] addr=0x%02X(%s) type=0x%02X len=%u |",
                addr, aname, type, plen);
            size_t show = plen < 16 ? plen : 16;
            for (size_t i = 0; i < show; i++)
                Serial.printf(" %02X", s_buf[3 + i]);
            if (plen > 16) Serial.print(" …");
            Serial.println();

            if (type == 0x29)
                Serial.println("  *** DEVICE_INFO — Ranger Micro acknowledged us! ***");
            if (type == 0x3A)
                Serial.println("  (RADIO_ID timing sync — Ranger Micro timing frame)");
        }

        memmove(s_buf, s_buf + total, s_len - total);
        s_len -= total;
    }
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("=== crsf-test firmware ===");
    Serial.printf("Serial1: RX=GPIO%d  TX=GPIO%d  baud=%u\n",
        CRSF_RX, CRSF_TX, CRSF_BAUD);
    Serial.println("Sending: RC@50Hz (0xEE) + DEVICE_PING@1Hz (0xEE→0xEA)");
    Serial.println("Parsing: all address bytes; printing non-echo frames only");
    Serial.println("Watch for addr=0xEA (Ranger Micro→handset) and type=0x29 (DEVICE_INFO)");
    Serial.println("────────────────────────────────────────────────────────");
    Serial1.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX, CRSF_TX);
}

void loop() {
    unsigned long now = millis();

    // Drain Serial1 into parse buffer.
    while (Serial1.available() && s_len < sizeof(s_buf))
        s_buf[s_len++] = (uint8_t)Serial1.read();
    parse_stream();

    // 50 Hz RC frames.
    static unsigned long s_rc_ms = 0;
    if (now - s_rc_ms >= 20) {
        s_rc_ms = now;
        send_rc_frame();
    }

    // 1 Hz DEVICE_PING.
    static unsigned long s_ping_ms = 0;
    if (now - s_ping_ms >= 1000) {
        s_ping_ms = now;
        send_device_ping();
    }

    // 5 s summary.
    static unsigned long s_sum_ms  = 0;
    static unsigned long s_boot_ms = millis();
    if (now - s_sum_ms >= 5000) {
        print_summary(now - s_boot_ms);
        s_sum_ms = now;
    }
}
