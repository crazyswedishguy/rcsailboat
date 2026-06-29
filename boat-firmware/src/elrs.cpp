// elrs.cpp — ELRS / CRSF receive path.
//
// AlfredoCRSF handles frame sync, CRC, and RC_CHANNELS_PACKED (0x16) decoding.
// Link statistics (RSSI, LQ) are read via getLinkStatistics() each update.
// The restart trigger (CH_RESTART held ≥2 s while disarmed) is checked here
// so it works regardless of which failsafe state the system is in.

#include "elrs.h"
#include "config.h"
#include "protocol.h"
#include <Arduino.h>
#include <AlfredoCRSF.h>

// ── Constants ──────────────────────────────────────────────────────────────────
static constexpr uint32_t RESTART_HOLD_MS = 2000;

// ── State ──────────────────────────────────────────────────────────────────────
static AlfredoCRSF s_crsf;
static uint32_t    s_restart_held_ms = 0;
static bool        s_restart_active  = false;

// ── Normalisation ──────────────────────────────────────────────────────────────
static float to_bipolar(int raw)
{
    // AlfredoCRSF returns µs-range values (172–1811), centre 992 → –1.0..+1.0
    float v = (float)(raw - CRSF_CHANNEL_VALUE_MID)
            / (float)(CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MID);
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

static float to_unipolar(int raw)
{
    // 172–1811 → 0.0..+1.0  (sail trim)
    float v = (float)(raw - CRSF_CHANNEL_VALUE_MIN)
            / (float)(CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN);
    return v > 1.0f ? 1.0f : v < 0.0f ? 0.0f : v;
}

// ── Public API ─────────────────────────────────────────────────────────────────
void elrs_init()
{
    Serial1.begin(CRSF_BAUD, SERIAL_8N1, pins::CRSF_RX, pins::CRSF_TX);
    s_crsf.begin(Serial1);
    Serial.printf("elrs: UART1 @ %lu baud  RX=GPIO%u  TX=GPIO%u\n",
                  (unsigned long)CRSF_BAUD, pins::CRSF_RX, pins::CRSF_TX);
}

void elrs_update()
{
    s_crsf.update();

    if (!s_crsf.isLinkUp()) {
        s_restart_active = false;
        return;
    }

    // ── CH_RESTART: hold at max while disarmed → soft reboot ─────────────────
    bool restart_high = s_crsf.getChannel(CH_RESTART + 1) > 1750;
    bool disarmed     = s_crsf.getChannel(CH_ARM     + 1) < 1000;

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

    // ── 10 Hz channel debug print ─────────────────────────────────────────────
    static uint32_t s_dbg_ms = 0;
    if (millis() - s_dbg_ms >= 100) {
        s_dbg_ms = millis();
        Serial.printf("elrs: rud=%+.2f sail=%.2f thr=%+.2f arm=%.2f mode=%+.2f  LQ=%u%%  RSSI=-%udBm\n",
                      elrs_get_channel(CH_RUDDER),
                      elrs_get_channel(CH_SAIL),
                      elrs_get_channel(CH_THROTTLE),
                      elrs_get_channel(CH_ARM),
                      elrs_get_channel(CH_MODE),
                      elrs_link_quality(), elrs_rssi());
    }
}

float elrs_get_channel(int ch)
{
    int raw = s_crsf.getChannel(ch + 1);  // AlfredoCRSF is 1-indexed
    if (ch == CH_SAIL) return to_unipolar(raw);
    return to_bipolar(raw);
}

bool elrs_link_ok()
{
    // AlfredoCRSF sets _linkIsUp = false after CRSF_FAILSAFE_STAGE1_MS (300 ms)
    // without a valid packet — conservative enough for our 500 ms failsafe window.
    return s_crsf.isLinkUp();
}

uint8_t elrs_rssi()
{
    return s_crsf.getLinkStatistics()->uplink_RSSI_1;
}

uint8_t elrs_link_quality()
{
    return s_crsf.getLinkStatistics()->uplink_Link_quality;
}

void elrs_send_frame(const uint8_t *frame, size_t len)
{
    Serial1.write(frame, len);
}
