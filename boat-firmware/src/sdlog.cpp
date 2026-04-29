#include "sdlog.h"
#include "config.h"
#include "elrs.h"
#include "imu.h"
#include "power.h"
#include "servos.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#ifdef GPS_ENABLED
#include "gps.h"
#endif

// SPI3 (HSPI) — independent from the display which owns SPI2 (FSPI).
static SPIClass s_spi(HSPI);
static File     s_file;
static bool     s_ready = false;

static const char *CSV_HEADER =
    "millis_ms,gps_fix,lat,lng,speed_kmh,heading_deg,altitude_m,satellites,"
    "voltage_v,current_a,mah_used,roll_deg,pitch_deg,"
    "lq_pct,rssi_dbm,rudder,sail,throttle,mcu_temp_c";

static bool open_log_file()
{
    for (int n = 0; n < 10000; n++) {
        char name[20];
        snprintf(name, sizeof(name), "/LOG%04d.CSV", n);
        if (!SD.exists(name)) {
            s_file = SD.open(name, FILE_WRITE);
            if (!s_file) return false;
            s_file.println(CSV_HEADER);
            s_file.flush();
            Serial.printf("sdlog: opened %s\n", name);
            return true;
        }
    }
    Serial.println("sdlog: no free log slots (LOG0000-LOG9999 all exist)");
    return false;
}

void sdlog_init()
{
    s_spi.begin(pins::SD_SCLK, pins::SD_MISO, pins::SD_MOSI);
    if (!SD.begin(pins::SD_CS, s_spi)) {
        Serial.println("sdlog: no SD card — logging disabled");
        return;
    }
    Serial.printf("sdlog: card mounted, %llu MB\n",
                  SD.totalBytes() / (1024ULL * 1024ULL));
    if (!open_log_file()) return;
    s_ready = true;
}

bool sdlog_is_ready() { return s_ready; }

void sdlog_update()
{
    if (!s_ready) return;

#ifdef GPS_ENABLED
    bool    gps_fix  = gps_has_fix();
    double  lat      = gps_lat();
    double  lng      = gps_lng();
    float   spd      = gps_speed_kmh();
    float   hdg      = gps_heading_deg();
    float   alt      = gps_altitude_m();
    uint8_t sats     = gps_satellites();
#else
    int     gps_fix  = 0;
    double  lat      = 0.0;
    double  lng      = 0.0;
    float   spd      = 0.0f;
    float   hdg      = 0.0f;
    float   alt      = 0.0f;
    uint8_t sats     = 0;
#endif

    float    v    = power_voltage_v();
    float    amps = power_current_a();
    float    mah  = power_mah_used();
    float    roll = imu_roll_deg();
    float    pit  = imu_pitch_deg();
    unsigned lq   = (unsigned)elrs_link_quality();
    unsigned rssi = (unsigned)elrs_rssi();
    float    rud  = servos_get_commanded(pwm_ch::RUDDER);
    float    sail = servos_get_commanded(pwm_ch::SAIL_WINCH);
    float    thr  = servos_get_commanded(pwm_ch::MOTOR_ESC);
    float    temp = (float)temperatureRead();

    // Write one CSV row. lat/lng need 7 decimal places (~1 cm resolution).
    s_file.printf(
        "%lu,%d,%.7f,%.7f,%.2f,%.2f,%.1f,%u,"
        "%.3f,%.3f,%.1f,%.2f,%.2f,"
        "%u,%u,%.3f,%.3f,%.3f,%.1f\n",
        millis(), (int)gps_fix, lat, lng, spd, hdg, alt, (unsigned)sats,
        v, amps, mah, roll, pit,
        lq, rssi, rud, sail, thr, temp);

    s_file.flush();
}
