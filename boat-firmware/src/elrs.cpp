// elrs.cpp — SX1262 GFSK radio driver (boat side).
// (Was LoRa SF7/BW500 during initial bring-up; switched to GFSK for higher
// throughput at the ~500 m range this link actually needs — the DIO1/SPI
// pitfalls documented below apply equally to both. See docs/lora-bringup.md.)
//
// The boat's SX1262 is wired on software SPI because both hardware SPI buses
// are occupied (SPI2 = CO5300 AMOLED display via QSPI, SPI3 = TF card).
// Bit-banging is implemented as a RadioLibHal subclass (BitBangHal, below) —
// NOT by subclassing SPIClass. SPIClass's begin()/transfer()/etc. are not
// virtual, and RadioLib's ArduinoHal stores the SPI object as a plain
// `SPIClass*`, so calls through it resolve to SPIClass's real hardware
// implementation at compile time regardless of the object's actual runtime
// type — a `SoftSPI : public SPIClass` override is invisible to RadioLib.
// (This was the root cause of a real bring-up failure: "SX1262 begin failed
// (err -2)" with all-zero SPI readback on real hardware, reproduced
// identically across two different physical modules — RadioLib was silently
// talking to the unconfigured onboard HSPI peripheral instead of bit-banging
// GPIO5/6/7. RadioLibHal's spiBegin/spiTransfer/etc. genuinely are virtual,
// so subclassing it and using Module's Hal-based constructor is the correct
// extension point for non-standard SPI.)
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
#include "soc/gpio_struct.h"

// ── Bit-banged SPI HAL ────────────────────────────────────────────────────────
// RadioLibHal is RadioLib's genuine extension point for non-standard SPI:
// its spiBegin/spiBeginTransaction/spiTransfer/spiEndTransaction/spiEnd are
// real virtual methods (see Hal.h), unlike SPIClass. Everything not related
// to SPI just delegates straight to the normal Arduino GPIO/timing calls.
class BitBangHal : public RadioLibHal {
public:
    BitBangHal(uint8_t clk, uint8_t miso, uint8_t mosi)
        : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
          _clk(clk), _miso(miso), _mosi(mosi),
          _clkMask(1UL << clk), _mosiMask(1UL << mosi), _misoMask(1UL << miso) {}

    void pinMode(uint32_t pin, uint32_t mode) override {
        if (pin == RADIOLIB_NC) return;
        ::pinMode(pin, mode);
    }
    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin == RADIOLIB_NC) return;
        ::digitalWrite(pin, value);
    }
    uint32_t digitalRead(uint32_t pin) override {
        if (pin == RADIOLIB_NC) return 0;
        return ::digitalRead(pin);
    }
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
        if (interruptNum == RADIOLIB_NC) return;
        ::attachInterrupt(interruptNum, interruptCb, mode);
    }
    void detachInterrupt(uint32_t interruptNum) override {
        if (interruptNum == RADIOLIB_NC) return;
        ::detachInterrupt(interruptNum);
    }
    void delay(RadioLibTime_t ms) override { ::delay(ms); }
    void delayMicroseconds(RadioLibTime_t us) override { ::delayMicroseconds(us); }
    RadioLibTime_t millis() override { return ::millis(); }
    RadioLibTime_t micros() override { return ::micros(); }
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override {
        if (pin == RADIOLIB_NC) return 0;
        return ::pulseIn(pin, state, timeout);
    }

    // Initialise GPIO directions. Do NOT touch any hardware SPI peripheral —
    // both are already claimed (SPI2 = display, SPI3 = TF card).
    void spiBegin() override {
        ::pinMode(_clk,  OUTPUT); ::digitalWrite(_clk,  LOW);
        ::pinMode(_mosi, OUTPUT); ::digitalWrite(_mosi, LOW);
        ::pinMode(_miso, INPUT);
    }
    void spiBeginTransaction() override {}
    void spiEndTransaction() override {}
    void spiEnd() override {}

    // Bit-bang SPI mode 0 (CPOL=0, CPHA=0), MSB first, via direct GPIO
    // register access (GPIO.out_w1ts/w1tc/in) rather than digitalWrite()/
    // digitalRead(). Arduino's GPIO wrapper calls carry real per-call
    // overhead (pin validation, mode dispatch) that was previously the
    // dominant cost of every SX1262 SPI transaction — this cuts it to a
    // handful of register accesses per bit. CLK/MOSI/MISO are all GPIO<32
    // on this board, so the low out_w1ts/out_w1tc/in registers cover them.
    // Empirically verified clean (no CRC failures / no chip-not-found) over
    // extended runs on real hardware — see docs/pinmap.md SX1262 notes.
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
        for (size_t n = 0; n < len; n++) {
            uint8_t data = out[n];
            uint8_t rx = 0;
            for (int8_t i = 7; i >= 0; i--) {
                if ((data >> i) & 1) GPIO.out_w1ts = _mosiMask;
                else                 GPIO.out_w1tc = _mosiMask;
                GPIO.out_w1ts = _clkMask;
                rx = (uint8_t)((rx << 1) | ((GPIO.in & _misoMask) ? 1 : 0));
                GPIO.out_w1tc = _clkMask;
            }
            in[n] = rx;
        }
    }

private:
    const uint8_t  _clk, _miso, _mosi;
    const uint32_t _clkMask, _mosiMask, _misoMask;
};

// ── RadioLib objects ───────────────────────────────────────────────────────────
// irq pin is RADIOLIB_NC (DIO1 not wired) — see file header comment.
static BitBangHal s_hal(pins::SX_CLK, pins::SX_MISO, pins::SX_MOSI);
static Module     s_module(&s_hal, pins::SX_CS, RADIOLIB_NC, pins::SX_RESET,
                            pins::SX_BUSY);
static SX1262     s_radio(&s_module);

// ── State ──────────────────────────────────────────────────────────────────────
// Raw CRSF channel values (172–1811); updated on every valid received frame.
static float    s_ch[16]     = {};
static uint32_t s_last_rx_ms = 0;  // millis() of the most recent valid RX frame
static int16_t  s_rssi_raw   = 0;  // last RSSI from RadioLib (negative dBm)

// Counts for LQ estimation: packets received in the current 1-second window.
static uint32_t s_pkt_window_start  = 0;
static uint8_t  s_pkt_count         = 0;  // RX count in the current 1-second window

// TEMP: measures the XIAO's actual achieved RC transmit rate (as observed on
// the boat's receive side), to answer "what framerate are we really getting"
// after widening TELEM_WAIT_MS. min/max inter-packet interval shows jitter.
static uint32_t s_rate_window_start = 0;
static uint16_t s_rate_pkt_count    = 0;
static uint32_t s_rate_min_gap_ms   = 0xFFFFFFFF;
static uint32_t s_rate_max_gap_ms   = 0;
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

// ── Process one received packet ────────────────────────────────────────────────
static void process_rx_packet()
{
    uint32_t rx_ms = millis();  // TEMP: round-trip timing diagnostic
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
    uint32_t now_rx = millis();
    // TEMP: real-world RC receive rate measurement.
    if (s_last_rx_ms != 0) {
        uint32_t gap = now_rx - s_last_rx_ms;
        if (gap < s_rate_min_gap_ms) s_rate_min_gap_ms = gap;
        if (gap > s_rate_max_gap_ms) s_rate_max_gap_ms = gap;
    }
    s_rate_pkt_count++;
    if (s_rate_window_start == 0) s_rate_window_start = now_rx;
    if (now_rx - s_rate_window_start >= 2000) {
        float hz = s_rate_pkt_count * 1000.0f / (now_rx - s_rate_window_start);
        Serial.printf("elrs: RC rate: %.1f Hz  gap min=%lu max=%lu ms  (%u pkts / %lu ms)\n",
                      (double)hz, s_rate_min_gap_ms, s_rate_max_gap_ms,
                      s_rate_pkt_count, now_rx - s_rate_window_start);
        s_rate_window_start = now_rx;
        s_rate_pkt_count = 0;
        s_rate_min_gap_ms = 0xFFFFFFFF;
        s_rate_max_gap_ms = 0;
    }
    s_last_rx_ms = now_rx;

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
    //
    // RadioLib's blocking transmit() waits for TX_DONE by polling the DIO1
    // GPIO directly (SX126x.cpp: `hal->digitalRead(mod->getIrq())`), not via
    // SPI. DIO1 is RADIOLIB_NC here (see file header), so that read always
    // returns 0 and transmit() would spin to timeout on every single call
    // without ever actually detecting completion. Do the same thing we do
    // for RX: kick off the send with startTransmit(), then poll TX_DONE
    // ourselves via the SPI-based getIrqFlags().
    if (s_tx_len > 0) {
        uint32_t toa_us = (uint32_t)s_radio.getTimeOnAir(s_tx_len);  // TEMP: diagnostic
        int tx_state = s_radio.startTransmit(s_tx_buf, s_tx_len);
        if (tx_state == RADIOLIB_ERR_NONE) {
            // Same timeout formula RadioLib's own transmit() uses.
            uint32_t timeout_ms = 5 + (uint32_t)(s_radio.getTimeOnAir(s_tx_len) * 5 / 1000);
            uint32_t tx_start = millis();
            while (!(s_radio.getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE)) {
                if (millis() - tx_start > timeout_ms) {
                    tx_state = RADIOLIB_ERR_TX_TIMEOUT;
                    break;
                }
            }
            if (tx_state == RADIOLIB_ERR_NONE)
                tx_state = s_radio.finishTransmit();
        }
        if (tx_state != RADIOLIB_ERR_NONE)
            Serial.printf("elrs: TX error %d\n", tx_state);
        // TEMP: round-trip timing diagnostic — how long from RC-frame-decoded
        // to reply-TX-complete. XIAO only listens for TELEM_WAIT_MS after its
        // own TX; if this consistently exceeds that, replies are being sent
        // too late for the XIAO to catch, even though the boat's TX itself
        // succeeds.
        Serial.printf("elrs: TX took %lu ms total, %lu.%03lu ms radio-calculated ToA, len=%u (state=%d)\n",
                      millis() - rx_ms, toa_us / 1000, toa_us % 1000, (unsigned)s_tx_len, tx_state);
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
    // RadioLibHal::init() defaults to a no-op (only ArduinoHal auto-calls
    // spiBegin()), so call it explicitly before touching the radio.
    s_hal.spiBegin();
    s_radio.setRfSwitchPins(pins::SX_RXEN, pins::SX_TXEN);

    int state = s_radio.beginFSK(
        FSK_FREQ_MHZ, FSK_BIT_RATE_KBPS, FSK_FREQ_DEV_KHZ, FSK_RX_BW_KHZ,
        FSK_POWER_DBM, FSK_PREAMBLE_BITS);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("elrs: SX1262 begin failed (err %d)\n", state);

        // Diagnostic: err -2 (CHIP_NOT_FOUND) means the version-string
        // register readback didn't match, which RadioLib reports whenever
        // the SPI status byte comes back 0x00 or 0xFF — i.e. it can't tell
        // "wrong chip" from "nothing is actually talking back". Dump the
        // raw bytes and idle pin levels so a wiring fault is visible
        // instead of just the error code.
        uint8_t ver[16] = {0};
        s_module.SPIreadRegisterBurst(0x0320 /* RADIOLIB_SX126X_REG_VERSION_STRING */,
                                       sizeof(ver), ver);
        Serial.print("elrs: raw version-string bytes:");
        for (size_t i = 0; i < sizeof(ver); i++) Serial.printf(" %02X", ver[i]);
        Serial.println();
        Serial.printf(
            "elrs: idle levels — BUSY(GPIO%u)=%d  MISO(GPIO%u)=%d  "
            "(CLK=GPIO%u MOSI=GPIO%u CS=GPIO%u RESET=GPIO%u)\n",
            pins::SX_BUSY, digitalRead(pins::SX_BUSY),
            pins::SX_MISO, digitalRead(pins::SX_MISO),
            pins::SX_CLK, pins::SX_MOSI, pins::SX_CS, pins::SX_RESET);
        return;
    }

    s_radio.startReceive();

    s_pkt_window_start = millis();

    Serial.printf("elrs: SX1262 ready  %.0f MHz FSK  %.0f kbps  dev=%.0f kHz  CS=GPIO%u (DIO1 unwired — polled)\n",
                  (double)FSK_FREQ_MHZ, (double)FSK_BIT_RATE_KBPS, (double)FSK_FREQ_DEV_KHZ,
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
