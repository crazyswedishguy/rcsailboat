// elrs.cpp — SX1262 LoRa radio driver (boat side).
//
// The boat's SX1262 is wired on software SPI because both hardware SPI buses
// are occupied (SPI2 = CO5300 AMOLED display via QSPI, SPI3 = TF card).
// A thin SoftSPI class (below) provides the SPIClass interface via bit-bang
// so RadioLib's Module constructor can accept it unchanged.
//
// DIO1 is intentionally left unconnected: this board has no free GPIO for it
// (GPIO42, the only header candidate, turned out to be reserved internally
// for AMOLED_EN — see docs/pinmap.md "Free GPIOs"). Instead of a hardware
// interrupt, elrs_update() polls the SX1262's IRQ status register over SPI
// (getIrqFlags()) once per loop() iteration. This costs nothing functionally:
// the old DIO1 ISR only ever set a flag that was drained on the next
// elrs_update() call anyway, so processing already happened at loop() cadence
// either way — polling just reads that same status a different way.
//
// Frame format over the air:
//   XIAO → Boat: CRSF RC_CHANNELS_PACKED (0x16), 26 bytes, up to 50 Hz.
//   Boat → XIAO: any CRSF telemetry frame built by telemetry.cpp.
// Using the CRSF binary format means the same encoding/decoding helpers
// already in the XIAO and base-station code can be reused without changes.
//
// TX / RX sequencing (half-duplex):
//   The radio stays in continuous-receive (startReceive) at all times.
//   When a queued telemetry frame is waiting (elrs_send_frame called), the
//   radio switches to TX immediately after the next RC frame is decoded, then
//   returns to startReceive.  This gives the XIAO enough time to switch to RX
//   after its own TX completes before the boat replies.

#include "elrs.h"
#include "config.h"
#include "protocol.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

// ── Software SPI ───────────────────────────────────────────────────────────────
// Inherits from SPIClass so it can be passed to RadioLib's Module constructor.
// Only begin(), beginTransaction(), endTransaction() and transfer(uint8_t) are
// needed by RadioLib; the rest of SPIClass is left as the do-nothing base.
class SoftSPI : public SPIClass {
public:
    SoftSPI(uint8_t clk, uint8_t miso, uint8_t mosi)
        : SPIClass(), _clk(clk), _miso(miso), _mosi(mosi) {}

    // Initialise GPIO directions; do NOT call SPIClass::begin() — that would
    // try to claim a hardware SPI peripheral which is already in use.
    void begin() {
        pinMode(_clk,  OUTPUT); digitalWrite(_clk,  LOW);
        pinMode(_mosi, OUTPUT); digitalWrite(_mosi, LOW);
        pinMode(_miso, INPUT);
    }

    // RadioLib calls these around every SPI transaction; no setup needed.
    void beginTransaction(SPISettings) {}
    void endTransaction() {}

    // Bit-bang SPI mode 0 (CPOL=0, CPHA=0): data sampled on rising edge.
    // 1 MHz effective clock rate (1 µs per half-period) — ample for SX1262.
    uint8_t transfer(uint8_t data) {
        uint8_t rx = 0;
        for (int8_t i = 7; i >= 0; i--) {
            digitalWrite(_mosi, (data >> i) & 1);
            digitalWrite(_clk, HIGH);
            delayMicroseconds(1);
            rx = (rx << 1) | (uint8_t)digitalRead(_miso);
            digitalWrite(_clk, LOW);
            delayMicroseconds(1);
        }
        return rx;
    }

private:
    const uint8_t _clk, _miso, _mosi;
};

// ── RadioLib objects ───────────────────────────────────────────────────────────
// irq pin is RADIOLIB_NC (DIO1 not wired) — see file header comment.
static SoftSPI s_spi(pins::SX_CLK, pins::SX_MISO, pins::SX_MOSI);
static Module  s_module(pins::SX_CS, RADIOLIB_NC, pins::SX_RESET,
                        pins::SX_BUSY, s_spi);
static SX1262  s_radio(&s_module);

// ── State ──────────────────────────────────────────────────────────────────────
// Raw CRSF channel values (172–1811); updated on every valid received frame.
static float    s_ch[16]     = {};
static uint32_t s_last_rx_ms = 0;  // millis() of the most recent valid RX frame
static int16_t  s_rssi_raw   = 0;  // last RSSI from RadioLib (negative dBm)

// Counts for LQ estimation: packets received in the current 1-second window.
static uint32_t s_pkt_window_start  = 0;
static uint8_t  s_pkt_count         = 0;  // RX count in the current 1-second window
static uint8_t  s_lq_pct            = 0;  // computed LQ, refreshed every second

// Pending telemetry: set by elrs_send_frame(), consumed on the next TX turn.
static uint8_t  s_tx_buf[64];
static size_t   s_tx_len = 0;

// ── CRC-8/DVB-S2 (polynomial 0xD5) ───────────────────────────────────────────
static uint8_t crc8_dvb_s2(const uint8_t *buf, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1);
    }
    return crc;
}

// ── Unpack 16 × 11-bit CRSF channel values from a 22-byte payload ─────────────
static void unpack_channels(const uint8_t payload[22])
{
    for (int i = 0; i < 16; i++) {
        int bit_pos  = i * 11;
        int byte_idx = bit_pos >> 3;
        int bit_off  = bit_pos & 7;
        uint32_t val = (uint32_t)payload[byte_idx]
                     | ((uint32_t)payload[byte_idx + 1] << 8);
        if (bit_off > 5)
            val |= (uint32_t)payload[byte_idx + 2] << 16;
        s_ch[i] = (float)((val >> bit_off) & 0x7FF);
    }
}

// Normalise a CRSF raw value (172–1811, centre 992) to –1.0..+1.0.
static float to_bipolar(float raw)
{
    float v = (raw - 992.0f) / (1811.0f - 992.0f);
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

// Normalise a CRSF raw value (172–1811) to 0.0..+1.0.
static float to_unipolar(float raw)
{
    float v = (raw - 172.0f) / (1811.0f - 172.0f);
    return v > 1.0f ? 1.0f : v < 0.0f ? 0.0f : v;
}

// ── Process one received LoRa packet ──────────────────────────────────────────
static void process_rx_packet()
{
    uint8_t buf[64];
    size_t  len = sizeof(buf);
    // Read packet and capture RSSI; BUSY must be low before this call.
    int state = s_radio.readData(buf, len);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("elrs: RX error %d\n", state);
        return;
    }
    s_rssi_raw = (int16_t)s_radio.getRSSI();

    // Expect a CRSF RC_CHANNELS_PACKED frame: [0xEE][24][0x16][22B payload][CRC]
    // Total 26 bytes.  Accept any CRSF sync byte (0xC8 or 0xEE).
    if (len < 26) return;
    uint8_t frame_len = buf[1];       // length field
    if (frame_len < 24) return;       // too short
    size_t total = 2u + frame_len;    // sync(1) + len(1) + frame_len
    if (total > len) return;
    if (buf[2] != 0x16) return;       // must be RC_CHANNELS_PACKED
    // Validate CRC (covers type byte + payload, excludes sync and len).
    uint8_t expected_crc = crc8_dvb_s2(buf + 2, frame_len - 1);
    if (expected_crc != buf[total - 1]) {
        Serial.println("elrs: CRC fail");
        return;
    }

    unpack_channels(buf + 3);
    s_last_rx_ms = millis();

    // Update rolling LQ counter.
    s_pkt_count++;
    uint32_t now = millis();
    if (now - s_pkt_window_start >= 1000) {
        // Expected 50 pkts/s; cap at 100 %.
        uint32_t lq = (uint32_t)s_pkt_count * 2u;
        s_lq_pct = lq > 100u ? 100u : (uint8_t)lq;
        s_pkt_count = 0;
        s_pkt_window_start = now;
    }

    // If a telemetry frame is queued, send it now while the XIAO is in RX.
    // Transmit → return to startReceive.
    if (s_tx_len > 0) {
        int tx_state = s_radio.transmit(s_tx_buf, s_tx_len);
        if (tx_state != RADIOLIB_ERR_NONE)
            Serial.printf("elrs: TX error %d\n", tx_state);
        s_tx_len = 0;
        s_radio.startReceive();
    }
}

// ── CH_RESTART debounce ────────────────────────────────────────────────────────
static constexpr uint32_t RESTART_HOLD_MS = 2000;
static uint32_t s_restart_held_ms = 0;
static bool     s_restart_active  = false;

static void check_restart_channel()
{
    bool restart_high = s_ch[CH_RESTART] > 1750.0f;
    bool disarmed     = s_ch[CH_ARM]     < 1000.0f;

    if (restart_high && disarmed) {
        if (!s_restart_active) {
            s_restart_active  = true;
            s_restart_held_ms = millis();
        } else if (millis() - s_restart_held_ms >= RESTART_HOLD_MS) {
            Serial.println("elrs: CH_RESTART held — rebooting");
            delay(50);
            ESP.restart();
        }
    } else {
        s_restart_active = false;
    }
}

// ── Public API ─────────────────────────────────────────────────────────────────
void elrs_init()
{
    s_spi.begin();
    s_radio.setRfSwitchPins(pins::SX_RXEN, pins::SX_TXEN);

    int state = s_radio.begin(
        LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
        LORA_SYNC_WORD, LORA_POWER_DBM, LORA_PREAMBLE);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("elrs: SX1262 begin failed (err %d)\n", state);
        return;
    }

    s_radio.startReceive();

    s_pkt_window_start = millis();

    Serial.printf("elrs: SX1262 ready  %.0f MHz / BW%.0f / SF%u  CS=GPIO%u (DIO1 unwired — polled)\n",
                  (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, (unsigned)LORA_SF,
                  (unsigned)pins::SX_CS);
}

void elrs_update()
{
    // No DIO1 interrupt wired (see file header) — poll the IRQ status
    // register over SPI instead. readData() clears IRQ flags internally,
    // matching what the DIO1 ISR + startReceive() combo did before.
    if (s_radio.getIrqFlags() & RADIOLIB_SX126X_IRQ_RX_DONE) {
        process_rx_packet();
    }

    if (elrs_link_ok())
        check_restart_channel();

    // 5 Hz channel debug print.
    static uint32_t s_dbg_ms = 0;
    if (millis() - s_dbg_ms >= 200) {
        s_dbg_ms = millis();
        Serial.printf(
            "elrs: rud=%+.2f sail=%.2f thr=%+.2f arm=%.2f  LQ=%u%%  RSSI=%ddBm\n",
            elrs_get_channel(CH_RUDDER),  elrs_get_channel(CH_SAIL),
            elrs_get_channel(CH_THROTTLE), elrs_get_channel(CH_ARM),
            elrs_link_quality(), -(int)elrs_rssi());
    }
}

float elrs_get_channel(int ch)
{
    if (ch == CH_SAIL || ch == CH_THROTTLE)
        return to_unipolar(s_ch[ch]);
    return to_bipolar(s_ch[ch]);
}

bool elrs_link_ok()
{
    return s_last_rx_ms != 0 && (millis() - s_last_rx_ms) < 500;
}

uint8_t elrs_rssi()
{
    return (uint8_t)(-s_rssi_raw);   // RadioLib returns negative dBm; we return absolute value
}

uint8_t elrs_link_quality()
{
    return s_lq_pct;
}

void elrs_send_frame(const uint8_t *frame, size_t len)
{
    if (len > sizeof(s_tx_buf)) return;
    memcpy(s_tx_buf, frame, len);
    s_tx_len = len;
    // Actual transmission happens in process_rx_packet() after the next
    // incoming RC frame arrives, so the XIAO has time to enter RX mode.
}
