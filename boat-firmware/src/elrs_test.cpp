// elrs_test.cpp — Minimal ELRS channel receiver for bench testing.
//
// Built as [env:esp32-s3-elrs-only].  Nothing but UART1 CRSF + serial print.
// No WiFi, display, I2C, GPS, compass, or servos.
//
// Prints received channel values + link stats at 5 Hz over USB-CDC so the
// Python elrs-console.py tool can verify end-to-end channel pass-through from
// the XIAO (crsf-bridge) → Ranger Micro → RF → RP3 → Waveshare.
//
// Output format (one line per 200 ms when link is up):
//   LQ= 99 RSSI= 45 | rud=+0.500 sail=0.000 thr=0.000 arm=1 mode=1
//
// Channel indices (0-based, matching protocol.h / shared/protocol.py):
//   0=rudder (-1..+1)  1=sail (0..1)  2=throttle (0..1)
//   3=arm (0/1)        4=mode (0/1)

#include <Arduino.h>
#include <AlfredoCRSF.h>

// Raw CRSF range constants (AlfredoCRSF library values)
static constexpr int CRSF_MIN = 172;
static constexpr int CRSF_MID = 992;
static constexpr int CRSF_MAX = 1811;

static HardwareSerial s_uart(1);
static AlfredoCRSF    s_crsf;

// 172..1811 → -1.0..+1.0 centred at 992
static float bipolar(int raw)
{
    float v = (float)(raw - CRSF_MID) / (float)(CRSF_MAX - CRSF_MID);
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

// 172..1811 → 0.0..1.0
static float unipolar(int raw)
{
    float v = (float)(raw - CRSF_MIN) / (float)(CRSF_MAX - CRSF_MIN);
    return v > 1.0f ? 1.0f : v < 0.0f ? 0.0f : v;
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("=== elrs-test v1 ===");
    Serial.println("UART1: RX=GPIO16  TX=GPIO17  baud=420000");
    s_uart.begin(420000, SERIAL_8N1, 16, 17);
    s_crsf.begin(s_uart);
    Serial.println("Waiting for CRSF link...");
}

void loop()
{
    s_crsf.update();

    static unsigned long s_ms = 0;
    if (millis() - s_ms < 200) return;
    s_ms = millis();

    bool up = s_crsf.isLinkUp();
    if (!up) {
        Serial.println("LINK DOWN");
        return;
    }

    auto *ls   = s_crsf.getLinkStatistics();
    uint8_t lq   = ls ? ls->uplink_Link_quality : 0;
    uint8_t rssi = ls ? ls->uplink_RSSI_1       : 0;

    // AlfredoCRSF channels are 1-based; protocol channels are 0-based.
    int ch1 = s_crsf.getChannel(1);  // CH_RUDDER
    int ch2 = s_crsf.getChannel(2);  // CH_SAIL
    int ch3 = s_crsf.getChannel(3);  // CH_THROTTLE
    int ch4 = s_crsf.getChannel(4);  // CH_ARM
    int ch5 = s_crsf.getChannel(5);  // CH_MODE

    Serial.printf("LQ=%3u RSSI=-%3u | rud=%+.3f sail=%.3f thr=%.3f arm=%d mode=%d\n",
        lq, rssi,
        bipolar(ch1),
        unipolar(ch2),
        unipolar(ch3),
        unipolar(ch4) > 0.5f ? 1 : 0,
        unipolar(ch5) > 0.5f ? 1 : 0);
}
