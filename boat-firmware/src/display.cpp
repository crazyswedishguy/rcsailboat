#include "display.h"
#include "bilge.h"
#include "config.h"
#include "diag.h"
#include "elrs.h"
#include "imu.h"
#include "power.h"
#include "servos.h"

#ifdef GPS_ENABLED
#include "gps.h"
#endif
#ifdef COMPASS_ENABLED
#include "compass.h"
#endif
#include "wifi_ctrl.h"

#include <Arduino.h>
#include <Wire.h>
#include "esp_lcd_sh8601.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <esp_log.h>

#include <lvgl.h>

static const char *TAG = "display";

// ── Panel geometry ────────────────────────────────────────────────────────────
#define LCD_HOST      SPI2_HOST
#define LCD_H_RES     280
#define LCD_V_RES     456
#define LCD_X_OFFSET  0x14   // physical column offset — see Waveshare 06_LVGL_Test

// ── LVGL task parameters ──────────────────────────────────────────────────────
#define LVGL_BUF_ROWS      (LCD_V_RES / 4)
#define LVGL_TICK_MS       2
#define LVGL_TASK_STACK    (16 * 1024)  // 16 KB — 6-screen UI needs headroom; 8 KB caused stack overflow
#define LVGL_TASK_PRIORITY 2

// ── Panel init sequence (verbatim from Waveshare 06_LVGL_Test/lcd_bsp.c) ─────
static const uint8_t s_c4[]     = {0x80};
static const uint8_t s_35[]     = {0x00};
static const uint8_t s_53[]     = {0x20};
static const uint8_t s_63[]     = {0xFF};
static const uint8_t s_51_dim[] = {0x00};
static const uint8_t s_51_brt[] = {0xFF};

static const sh8601_lcd_init_cmd_t s_init_cmds[] = {
    {0x11, nullptr,   0, 80},
    {0xC4, s_c4,      1,  0},
    {0x35, s_35,      1,  0},
    {0x53, s_53,      1,  1},
    {0x63, s_63,      1,  1},
    {0x51, s_51_dim,  1,  1},
    {0x29, nullptr,   0, 10},
    {0x51, s_51_brt,  1,  0},
};

// ── Module state ──────────────────────────────────────────────────────────────
static SemaphoreHandle_t s_mux      = nullptr;
static lv_disp_drv_t     s_drv;
static bool              s_ready   = false;

// Tileview reference + edge-tile pointers for wrap-around gesture handling
static lv_obj_t *s_tileview  = nullptr;
static lv_obj_t *s_tile_wifi = nullptr;   // col 0 — leftmost
static lv_obj_t *s_tile_att  = nullptr;   // col 5 — Attitude
static lv_obj_t *s_tile_diag = nullptr;   // col 6 — rightmost (Devices)

// ── Screen 0 (WiFi / ELRS control) — label handles ───────────────────────────
static lv_obj_t *s0_lbl_mode  = nullptr;   // "WIFI AP" / "ELRS CTRL"
static lv_obj_t *s0_lbl_info1 = nullptr;   // SSID or LQ
static lv_obj_t *s0_lbl_info2 = nullptr;   // IP or RSSI
static lv_obj_t *s0_lbl_info3 = nullptr;   // clients or link status
static lv_obj_t *s0_lbl_info4 = nullptr;   // last cmd age or duration
static lv_obj_t *s0_btn       = nullptr;
static lv_obj_t *s0_btn_lbl   = nullptr;

// ── Screen 1 (Main overview) — label handles ─────────────────────────────────
static lv_obj_t *s1_lbl_link   = nullptr;
static lv_obj_t *s1_lbl_rssi   = nullptr;
static lv_obj_t *s1_lbl_batt   = nullptr;
static lv_obj_t *s1_lbl_rudder = nullptr;
static lv_obj_t *s1_lbl_sail   = nullptr;
static lv_obj_t *s1_lbl_thr    = nullptr;
static lv_obj_t *s1_lbl_temp   = nullptr;
static lv_obj_t *s1_lbl_status = nullptr;   // capsize / bilge / OK

// ── Screen 2 (Link detail) ────────────────────────────────────────────────────
static lv_obj_t *s2_dot        = nullptr;   // status circle
static lv_obj_t *s2_lbl_status = nullptr;
static lv_obj_t *s2_arc_lq     = nullptr;
static lv_obj_t *s2_lbl_lq_pct = nullptr;
static lv_obj_t *s2_lbl_rssi   = nullptr;
static lv_obj_t *s2_lbl_dur    = nullptr;

// ── Screen 3 (Battery detail) ────────────────────────────────────────────────
static lv_obj_t *s3_lbl_volt   = nullptr;
static lv_obj_t *s3_bar_volt   = nullptr;
static lv_obj_t *s3_lbl_pct    = nullptr;
static lv_obj_t *s3_lbl_curr   = nullptr;
static lv_obj_t *s3_lbl_avg    = nullptr;
static lv_obj_t *s3_lbl_mah    = nullptr;
static lv_obj_t *s3_lbl_temp   = nullptr;

// ── Screen 4 (Controls) ──────────────────────────────────────────────────────
static lv_obj_t *s4_bar_rudder  = nullptr;
static lv_obj_t *s4_lbl_rudder  = nullptr;
static lv_obj_t *s4_bar_sail    = nullptr;
static lv_obj_t *s4_lbl_sail    = nullptr;
static lv_obj_t *s4_bar_thr     = nullptr;
static lv_obj_t *s4_lbl_thr     = nullptr;

// ── Screen 5 (Attitude) ──────────────────────────────────────────────────────
static lv_obj_t *s5_bar_roll    = nullptr;
// ── Screen 6 (Devices) ───────────────────────────────────────────────────────
static lv_obj_t *s6_dot[DEV_COUNT]        = {};
static lv_obj_t *s6_lbl_status[DEV_COUNT] = {};
static lv_obj_t *s6_btn[DEV_COUNT]        = {};
#ifdef GPS_ENABLED
static lv_obj_t *s5_lbl_speed  = nullptr;
#endif
static lv_obj_t *s5_lbl_roll    = nullptr;
static lv_obj_t *s5_bar_pitch   = nullptr;
static lv_obj_t *s5_lbl_pitch   = nullptr;
static lv_obj_t *s5_lbl_heading = nullptr;

// ── Touch cache (polled by main task, read by LVGL indev callback) ────────────
static lv_indev_state_t s_touch_state = LV_INDEV_STATE_REL;
static lv_point_t       s_touch_pt    = {};

// ── Session / link timing ─────────────────────────────────────────────────────
static unsigned long s_session_start_ms = 0;
static unsigned long s_link_start_ms   = 0;
static bool          s_was_linked      = false;

// ── Banner colors (one per screen) ───────────────────────────────────────────
#define C_WIFI  lv_color_make(230,  81,   0)   // Deep Orange 800
#define C_MAIN  lv_color_make(  0,  77,  64)   // Teal 900
#define C_LINK  lv_color_make( 13,  71, 161)   // Blue 800
#define C_BATT  lv_color_make(183,  28,  28)   // Red 900  (battery/power)
#define C_CTRL  lv_color_make( 74,  20, 140)   // Purple 900
#define C_ATT   lv_color_make( 26,  35, 126)   // Indigo 900
#define C_DIAG  lv_color_make( 31,  41,  55)   // Dark Blue Grey (system health)

// ── LVGL / panel callbacks ────────────────────────────────────────────────────
static bool flush_ready_cb(esp_lcd_panel_io_handle_t,
                            esp_lcd_panel_io_event_data_t *, void *user_ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *)user_ctx);
    return false;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel,
        area->x1 + LCD_X_OFFSET, area->y1,
        area->x2 + LCD_X_OFFSET + 1, area->y2 + 1, colors);
}

static void rounder_cb(lv_disp_drv_t *, lv_area_t *a)
{
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

static void tick_cb(void *) { lv_tick_inc(LVGL_TICK_MS); }

static void lvgl_task(void *)
{
    for (;;) {
        if (xSemaphoreTake(s_mux, portMAX_DELAY) == pdTRUE) {
            uint32_t ms = lv_timer_handler();
            xSemaphoreGive(s_mux);
            ms = ms > 500 ? 500 : (ms < 1 ? 1 : ms);
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
    }
}

static void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data)
{
    data->point = s_touch_pt;
    data->state = s_touch_state;
}

static bool lvgl_lock(int ms)
{
    return xSemaphoreTake(s_mux, ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void lvgl_unlock() { xSemaphoreGive(s_mux); }

// ── Color helpers ─────────────────────────────────────────────────────────────
static lv_color_t color_lq(unsigned lq) {
    if (lq >= 70) return lv_palette_main(LV_PALETTE_GREEN);
    if (lq >= 30) return lv_palette_main(LV_PALETTE_YELLOW);
    return lv_palette_main(LV_PALETTE_RED);
}
static lv_color_t color_rssi(unsigned rssi) {
    if (rssi <= 80)  return lv_palette_main(LV_PALETTE_GREEN);
    if (rssi <= 100) return lv_palette_main(LV_PALETTE_YELLOW);
    return lv_palette_main(LV_PALETTE_RED);
}
static lv_color_t color_batt(float v) {
    if (v >= 12.0f) return lv_palette_main(LV_PALETTE_GREEN);
    if (v >= 11.4f) return lv_palette_main(LV_PALETTE_YELLOW);
    return lv_palette_main(LV_PALETTE_RED);
}
static lv_color_t color_temp(float t) {
    if (t < 60.0f) return lv_palette_main(LV_PALETTE_GREEN);
    if (t < 80.0f) return lv_palette_main(LV_PALETTE_YELLOW);
    return lv_palette_main(LV_PALETTE_RED);
}
static lv_color_t color_angle(float deg) {
    float a = deg < 0 ? -deg : deg;
    if (a < 20.0f) return lv_palette_main(LV_PALETTE_GREEN);
    if (a < 45.0f) return lv_palette_main(LV_PALETTE_YELLOW);
    return lv_palette_main(LV_PALETTE_RED);
}
static int batt_pct(float v) {
    int p = (int)((v - 10.5f) / (12.6f - 10.5f) * 100.0f);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// ── Widget factory helpers ────────────────────────────────────────────────────

static lv_obj_t *make_rect(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, lv_coord_t h, lv_color_t c)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, c, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

static lv_obj_t *make_label(lv_obj_t *p, lv_coord_t y, const char *text)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_pos(l, 10, y);
    return l;
}

static lv_obj_t *make_label_font(lv_obj_t *p, lv_coord_t x, lv_coord_t y,
                                  const char *text, const lv_font_t *font, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, c, LV_PART_MAIN);
    lv_obj_set_pos(l, x, y);
    return l;
}

// Section sub-header (cyan Montserrat-14) + 1px separator line.
static void make_section(lv_obj_t *p, lv_coord_t y, const char *name)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_CYAN), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(l, 10, y);
    make_rect(p, 10, y + 18, LCD_H_RES - 20, 1, lv_palette_darken(LV_PALETTE_TEAL, 1));
}

// Full-width colored banner with centered title (Montserrat 20).
// Returns the banner object so callers can add children.
static lv_obj_t *make_banner(lv_obj_t *p, const char *title, lv_color_t bg)
{
    lv_obj_t *hdr = make_rect(p, 0, 0, LCD_H_RES, 50, bg);
    lv_obj_t *lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    // Bright accent line below banner
    make_rect(p, 0, 50, LCD_H_RES, 2, lv_color_lighten(bg, 80));
    return hdr;
}

// Horizontal bar widget (w=240, h=22) with dark track.
// symmetrical=true: fills left/right from centre (for rudder, throttle, roll, pitch).
static lv_obj_t *make_bar(lv_obj_t *p, lv_coord_t y, bool symmetrical, lv_color_t ind_c)
{
    const lv_coord_t W = 240, H = 22, X = (LCD_H_RES - W) / 2;
    lv_obj_t *b = lv_bar_create(p);
    lv_obj_set_size(b, W, H);
    lv_obj_set_pos(b, X, y);
    lv_obj_set_style_bg_color(b, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, ind_c, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
    if (symmetrical) {
        lv_bar_set_mode(b, LV_BAR_MODE_SYMMETRICAL);
        lv_bar_set_range(b, -100, 100);
        lv_bar_set_value(b, 0, LV_ANIM_OFF);
    } else {
        lv_bar_set_range(b, 0, 100);
        lv_bar_set_value(b, 0, LV_ANIM_OFF);
    }
    return b;
}

// ── Screen builders ───────────────────────────────────────────────────────────

static void mode_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    CtrlMode cur = wifi_ctrl_mode();
    wifi_ctrl_set_mode(cur == CtrlMode::WIFI ? CtrlMode::ELRS : CtrlMode::WIFI);
}

static void build_tile_wifi(lv_obj_t *t)
{
    make_banner(t, "WIFI / ELRS", C_WIFI);

    // Large mode indicator
    s0_lbl_mode = make_label_font(t, 10, 66, "WIFI AP",
                                  &lv_font_montserrat_28, lv_color_white());

    // Info lines
    s0_lbl_info1 = make_label(t, 114, "SSID: " WIFI_AP_SSID);
    s0_lbl_info2 = make_label(t, 142, "IP:   192.168.4.1");
    s0_lbl_info3 = make_label(t, 170, "Clients:  0");
    s0_lbl_info4 = make_label(t, 198, "Last cmd: ---");

    // Toggle button
    const lv_coord_t BW = 240, BH = 58, BX = (LCD_H_RES - BW) / 2;
    s0_btn = lv_btn_create(t);
    lv_obj_set_size(s0_btn, BW, BH);
    lv_obj_set_pos(s0_btn, BX, 264);
    lv_obj_set_style_bg_color(s0_btn, lv_color_make(13, 71, 161), LV_PART_MAIN);  // blue = "go ELRS"
    lv_obj_set_style_radius(s0_btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(s0_btn, mode_btn_cb, LV_EVENT_CLICKED, nullptr);

    s0_btn_lbl = lv_label_create(s0_btn);
    lv_label_set_text(s0_btn_lbl, "Enable ELRS");
    lv_obj_set_style_text_font(s0_btn_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s0_btn_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s0_btn_lbl);

    // Safety note
    make_label_font(t, 10, 346,
                    "Throttle zeroed when disarmed",
                    &lv_font_montserrat_14, lv_color_make(120, 120, 120));
    make_label_font(t, 10, 368,
                    "or if connection drops > 500ms",
                    &lv_font_montserrat_14, lv_color_make(100, 100, 100));
}

static void build_tile_main(lv_obj_t *t)
{
    // Title bar: "RC Sailboat" in Montserrat 28 + subtitle
    lv_obj_t *hdr = make_rect(t, 0, 0, LCD_H_RES, 64, C_MAIN);
    lv_obj_t *ttl = lv_label_create(hdr);
    lv_label_set_text(ttl, "RC Sailboat");
    lv_obj_set_style_text_color(ttl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, -9);
    lv_obj_t *sub = lv_label_create(hdr);
    lv_label_set_text(sub, "~ telemetry ~");
    lv_obj_set_style_text_color(sub, lv_palette_lighten(LV_PALETTE_TEAL, 3), LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 15);
    make_rect(t, 0, 64, LCD_H_RES, 2, lv_palette_main(LV_PALETTE_TEAL));

    make_section(t, 74, "LINK");
    s1_lbl_link = make_label(t, 100, "Link: --  LQ: --%");
    s1_lbl_rssi = make_label(t, 126, "RSSI: -- dBm");

    make_section(t, 160, "BATTERY");
    s1_lbl_batt = make_label(t, 186, "--.-- V   --.-- A");

    make_section(t, 218, "CONTROLS");
    s1_lbl_rudder = make_label(t, 244, "Rudder:   +0.000");
    s1_lbl_sail   = make_label(t, 270, "Sail:      0.000");
    s1_lbl_thr    = make_label(t, 296, "Throttle: +0.000");

    make_section(t, 330, "SYSTEM");
    s1_lbl_temp = make_label(t, 356, "MCU Temp: --\xC2\xB0""C");

    make_section(t, 390, "STATUS");
    s1_lbl_status = make_label_font(t, 10, 416, "Systems OK",
                                    &lv_font_montserrat_14,
                                    lv_palette_main(LV_PALETTE_GREEN));
}

static void build_tile_link(lv_obj_t *t)
{
    make_banner(t, "LINK", C_LINK);

    // Connection status dot + text
    s2_dot = make_rect(t, 10, 66, 18, 18, lv_palette_main(LV_PALETTE_RED));
    lv_obj_set_style_radius(s2_dot, 9, LV_PART_MAIN);

    s2_lbl_status = make_label_font(t, 34, 66, "NO LINK",
                                    &lv_font_montserrat_20, lv_palette_main(LV_PALETTE_RED));

    // LQ arc gauge — 150px, centered horizontally
    const lv_coord_t ARC_SZ = 150;
    const lv_coord_t AX = (LCD_H_RES - ARC_SZ) / 2;
    s2_arc_lq = lv_arc_create(t);
    lv_obj_set_size(s2_arc_lq, ARC_SZ, ARC_SZ);
    lv_obj_set_pos(s2_arc_lq, AX, 96);
    lv_arc_set_bg_angles(s2_arc_lq, 135, 45);   // 270° sweep, 0 at 12 o'clock
    lv_arc_set_range(s2_arc_lq, 0, 100);
    lv_arc_set_value(s2_arc_lq, 0);
    lv_obj_set_style_arc_color(s2_arc_lq, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s2_arc_lq, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s2_arc_lq, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s2_arc_lq, 14, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s2_arc_lq, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s2_arc_lq, 0, LV_PART_KNOB);
    lv_obj_clear_flag(s2_arc_lq, LV_OBJ_FLAG_CLICKABLE);

    // Big LQ% label centered inside the arc
    s2_lbl_lq_pct = lv_label_create(t);
    lv_label_set_text(s2_lbl_lq_pct, "0%");
    lv_obj_set_style_text_font(s2_lbl_lq_pct, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s2_lbl_lq_pct, lv_color_white(), LV_PART_MAIN);
    lv_obj_align_to(s2_lbl_lq_pct, s2_arc_lq, LV_ALIGN_CENTER, 0, 0);

    make_label_font(t, (LCD_H_RES-80)/2, 256, "Link Quality",
                    &lv_font_montserrat_14, lv_palette_lighten(LV_PALETTE_BLUE, 2));

    s2_lbl_rssi = make_label(t, 284, "RSSI: -- dBm");
    s2_lbl_dur  = make_label(t, 312, "Duration: 0:00:00");
}

static void build_tile_battery(lv_obj_t *t)
{
    make_banner(t, "BATTERY", C_BATT);

    // Large voltage readout
    s3_lbl_volt = make_label_font(t, 10, 64, "--.-- V",
                                  &lv_font_montserrat_28, lv_color_white());

    // Voltage / charge bar
    s3_bar_volt = make_bar(t, 104, false, lv_palette_main(LV_PALETTE_GREEN));

    s3_lbl_pct = make_label_font(t, 10, 132, "--%  charged",
                                 &lv_font_montserrat_14,
                                 lv_palette_lighten(LV_PALETTE_RED, 2));

    make_rect(t, 10, 158, LCD_H_RES - 20, 1, lv_color_make(60, 60, 60));

    s3_lbl_curr = make_label(t, 168, "Current:   --.-- A");
    s3_lbl_avg  = make_label(t, 198, "Avg draw:  --.-- A");
    s3_lbl_mah  = make_label(t, 228, "Used:       --- mAh");

    make_rect(t, 10, 264, LCD_H_RES - 20, 1, lv_color_make(60, 60, 60));

    s3_lbl_temp = make_label(t, 274, "MCU Temp: --\xC2\xB0""C");
}

static void build_tile_controls(lv_obj_t *t)
{
    make_banner(t, "CONTROLS", C_CTRL);

    lv_color_t pc = lv_palette_main(LV_PALETTE_PURPLE);

    make_section(t, 60, "RUDDER");
    s4_bar_rudder = make_bar(t, 86, true, pc);
    s4_lbl_rudder = make_label_font(t, (LCD_H_RES-60)/2, 116,
                                    "+0.000", &lv_font_montserrat_20, lv_color_white());

    make_section(t, 152, "SAIL TRIM");
    s4_bar_sail = make_bar(t, 178, false, lv_palette_lighten(LV_PALETTE_PURPLE, 2));
    s4_lbl_sail = make_label_font(t, (LCD_H_RES-60)/2, 208,
                                  "0.000", &lv_font_montserrat_20, lv_color_white());
    // Range hints
    make_label_font(t, 20, 208, "In", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
    make_label_font(t, 224, 208, "Out", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));

    make_section(t, 244, "THROTTLE");
    s4_bar_thr = make_bar(t, 270, true, pc);
    s4_lbl_thr = make_label_font(t, (LCD_H_RES-60)/2, 300,
                                 "+0.000", &lv_font_montserrat_20, lv_color_white());
    // Range hints
    make_label_font(t, 20, 300, "Rev", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
    make_label_font(t, 220, 300, "Fwd", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
}

static void build_tile_attitude(lv_obj_t *t)
{
    make_banner(t, "ATTITUDE", C_ATT);

    lv_color_t ic = lv_palette_main(LV_PALETTE_INDIGO);

    make_section(t, 60, "ROLL");
    s5_bar_roll = make_bar(t, 86, true, ic);
    // Endpoint hints
    make_label_font(t, 20, 116, "Port", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
    make_label_font(t, 204, 116, "Stbd", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
    lv_bar_set_range(s5_bar_roll, -90, 90);
    s5_lbl_roll = make_label_font(t, (LCD_H_RES-80)/2, 116,
                                  "+0.0\xC2\xB0", &lv_font_montserrat_20, lv_color_white());

    make_section(t, 152, "PITCH");
    s5_bar_pitch = make_bar(t, 178, true, ic);
    lv_bar_set_range(s5_bar_pitch, -90, 90);
    make_label_font(t, 20, 208, "Bow\xe2\x86\x93", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
    make_label_font(t, 204, 208, "Bow\xe2\x86\x91", &lv_font_montserrat_14,
                    lv_color_make(120,120,120));
    s5_lbl_pitch = make_label_font(t, (LCD_H_RES-80)/2, 208,
                                   "+0.0\xC2\xB0", &lv_font_montserrat_20, lv_color_white());

    make_section(t, 244, "HEADING");
#ifdef COMPASS_ENABLED
    s5_lbl_heading = make_label_font(t, (LCD_H_RES-140)/2, 280,
                                     "--- \xC2\xB0  ---",
                                     &lv_font_montserrat_28, lv_color_white());
#else
    s5_lbl_heading = make_label_font(t, 10, 272,
                                     "No compass fitted",
                                     &lv_font_montserrat_14,
                                     lv_color_make(120,120,120));
    make_label_font(t, 10, 296,
                    "Add HMC5883L + build with",
                    &lv_font_montserrat_14, lv_color_make(100,100,100));
    make_label_font(t, 10, 318,
                    "esp32-s3-compass env",
                    &lv_font_montserrat_14, lv_color_make(100,100,100));
#endif

#ifdef GPS_ENABLED
    make_section(t, 346, "SPEED");
    s5_lbl_speed = make_label_font(t, (LCD_H_RES - 130) / 2, 376,
                                   "--.- km/h",
                                   &lv_font_montserrat_28, lv_color_white());
#endif
}

// ── Wrap-around gesture handler ───────────────────────────────────────────────
// LVGL fires LV_EVENT_GESTURE on the tileview even when scroll is clamped at
// the boundary (elastic is disabled but gesture detection is independent).
// A right-swipe at col 0 (WiFi) jumps to col 5 (Attitude) and vice-versa.
static void tv_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t    dir  = lv_indev_get_gesture_dir(indev);
    lv_obj_t   *tile = lv_tileview_get_tile_act(s_tileview);
    if (tile == s_tile_wifi && dir == LV_DIR_RIGHT) {
        lv_obj_set_tile_id(s_tileview, 6, 0, LV_ANIM_ON);   // wrap → Devices
    } else if (tile == s_tile_diag && dir == LV_DIR_LEFT) {
        lv_obj_set_tile_id(s_tileview, 0, 0, LV_ANIM_ON);
    }
}

static void repair_btn_cb(lv_event_t *e)
{
    diag_request_reprobe((DevId)(uintptr_t)lv_event_get_user_data(e));
}

static void build_tile_diag(lv_obj_t *t)
{
    make_banner(t, "DEVICES", C_DIAG);

    // 4 rows of 101 px each, starting just below the banner accent line at y=52.
    for (uint8_t i = 0; i < DEV_COUNT; i++) {
        const DeviceInfo *d = diag_info((DevId)i);
        lv_coord_t ry = 52 + (lv_coord_t)i * 101;

        if (i > 0)
            make_rect(t, 0, ry, LCD_H_RES, 1, lv_color_make(50, 50, 50));

        // Status dot — color updated dynamically in display_update()
        s6_dot[i] = make_rect(t, 8, ry + 14, 20, 20, lv_palette_main(LV_PALETTE_GREY));
        lv_obj_set_style_radius(s6_dot[i], 10, LV_PART_MAIN);

        // Device name (static)
        char name_buf[12];
        snprintf(name_buf, sizeof(name_buf), "%s", d->name);
        make_label_font(t, 36, ry + 8, name_buf, &lv_font_montserrat_20, lv_color_white());

        // Address and role (static, grey)
        char meta_buf[24];
        snprintf(meta_buf, sizeof(meta_buf), "0x%02X  %s", d->addr, d->role);
        make_label_font(t, 36, ry + 36, meta_buf, &lv_font_montserrat_14,
                        lv_color_make(140, 140, 140));

        // Status text (dynamic — "OK" / "ABSENT")
        s6_lbl_status[i] = make_label_font(t, 36, ry + 60, "—",
                                           &lv_font_montserrat_14,
                                           lv_color_make(140, 140, 140));

        // Repair button — turns red when device is absent
        lv_obj_t *btn = lv_btn_create(t);
        lv_obj_set_size(btn, 82, 30);
        lv_obj_set_pos(btn, LCD_H_RES - 92, ry + 34);
        lv_obj_set_style_bg_color(btn, lv_color_make(55, 55, 55), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "REPAIR");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(btn, repair_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(uint8_t)i);
        s6_btn[i] = btn;
    }
}

// ── Assemble tileview with all 7 screens ─────────────────────────────────────
// Layout (col): [WiFi(0)] [Main(1)] [Link(2)] [Battery(3)] [Controls(4)] [Attitude(5)] [Devices(6)]
// Swipe right from WiFi → Devices (wrap).  Swipe left from Devices → WiFi (wrap).
// Boot view: Main (col 1).
static void build_screens()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_style_bg_color(tv, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_t *tw = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);  // WiFi (leftmost)
    lv_obj_t *t0 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);   // Main
    lv_obj_t *t1 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);   // Link
    lv_obj_t *t2 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);   // Battery
    lv_obj_t *t3 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_HOR);   // Controls
    lv_obj_t *t4 = lv_tileview_add_tile(tv, 5, 0, LV_DIR_HOR);   // Attitude
    lv_obj_t *t5 = lv_tileview_add_tile(tv, 6, 0, LV_DIR_LEFT);  // Devices (rightmost)

    for (lv_obj_t *t : {tw, t0, t1, t2, t3, t4, t5}) {
        lv_obj_set_style_bg_color(t, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
    }

    build_tile_wifi(tw);
    build_tile_main(t0);
    build_tile_link(t1);
    build_tile_battery(t2);
    build_tile_controls(t3);
    build_tile_attitude(t4);
    build_tile_diag(t5);

    // LVGL 8.3 requires at least one event listener registered directly on the
    // tileview for scroll-chain propagation from child tiles to reach it.
    // Without these, horizontal swipes on tiles are not forwarded to the tileview
    // and tile navigation silently breaks. Empty callbacks are sufficient.
    lv_obj_add_event_cb(tv, [](lv_event_t *) {}, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(tv, [](lv_event_t *) {}, LV_EVENT_VALUE_CHANGED, nullptr);

    // Store references for the wrap-around gesture callback
    s_tileview  = tv;
    s_tile_wifi = tw;
    s_tile_att  = t4;
    s_tile_diag = t5;
    // LVGL 8.3 fires LV_EVENT_GESTURE on the pressed tile, not the tileview.
    // Register on the two edge tiles only; also bubble swipes from the mode button.
    lv_obj_add_event_cb(tw, tv_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(t5, tv_gesture_cb, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_flag(s0_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Start on Main, not WiFi
    lv_obj_set_tile_id(tv, 1, 0, LV_ANIM_OFF);
}

// ── Public API ────────────────────────────────────────────────────────────────

void display_init()
{
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num     = (int)pins_unused::OLED_CLK;
    buscfg.data0_io_num    = (int)pins_unused::OLED_D0;
    buscfg.data1_io_num    = (int)pins_unused::OLED_D1;
    buscfg.data2_io_num    = (int)pins_unused::OLED_D2;
    buscfg.data3_io_num    = (int)pins_unused::OLED_D3;
    buscfg.max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num         = (int)pins_unused::OLED_CS;
    io_cfg.dc_gpio_num         = -1;
    io_cfg.spi_mode            = 0;
    io_cfg.pclk_hz             = 40 * 1000 * 1000;
    io_cfg.trans_queue_depth   = 10;
    io_cfg.on_color_trans_done = flush_ready_cb;
    io_cfg.user_ctx            = &s_drv;
    io_cfg.lcd_cmd_bits        = 32;
    io_cfg.lcd_param_bits      = 8;
    io_cfg.flags.quad_mode     = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    sh8601_vendor_config_t vendor = {};
    vendor.init_cmds             = s_init_cmds;
    vendor.init_cmds_size        = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]);
    vendor.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = (int)pins_unused::OLED_RESET;
    panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    panel_cfg.vendor_config  = &vendor;

    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    lv_init();

    // Allocate from PSRAM (8 MB OPI on this board) rather than internal SRAM.
    // MALLOC_CAP_DMA forced 124 KB of render buffer into the 327 KB internal heap,
    // leaving too little for WiFi WPA2-AES handshakes (causing "Failed to allocate
    // memory" and connection failures). ESP32-S3 GDMA can access OPI PSRAM.
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_ROWS * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_ROWS * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_ROWS);

    lv_disp_drv_init(&s_drv);
    s_drv.hor_res    = LCD_H_RES;
    s_drv.ver_res    = LCD_V_RES;
    s_drv.flush_cb   = flush_cb;
    s_drv.rounder_cb = rounder_cb;
    s_drv.draw_buf   = &draw_buf;
    s_drv.user_data  = panel;
    lv_disp_drv_register(&s_drv);

    // Touch input device — reads from cached values polled by main task
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // FT3168 init: write 0x00 to register 0x00 — puts controller in normal
    // operating mode (matches Waveshare 06_LVGL_Test/FT3168.cpp).
    if (diag_ok(DEV_FT3168)) {
        Wire.beginTransmission(i2c_addr::FT3168);
        Wire.write((uint8_t)0x00);
        Wire.write((uint8_t)0x00);
        if (Wire.endTransmission(true) != 0) {
            // FT3168 didn't ACK — bus may be stuck; recover before loop() starts.
            Serial.println("display: FT3168 init write failed — recovering bus");
            i2c_recover_bus();
        }
    }

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = tick_cb;
    tick_args.name     = "lvgl_tick";
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    s_mux = xSemaphoreCreateMutex();
    assert(s_mux);

    build_screens();

    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK, nullptr,
                LVGL_TASK_PRIORITY, nullptr);

    s_ready = true;
    ESP_LOGI(TAG, "display ready — 280x456 AMOLED, 5 screens");
}

// Called from main loop at ~50 Hz — polls FT3168 touch via Wire and caches
// the result so the LVGL task can read it safely from touch_read_cb().
void display_poll_touch()
{
    if (!diag_ok(DEV_FT3168)) {
        s_touch_state = LV_INDEV_STATE_REL;
        return;
    }

    static unsigned long s_err_since_ms = 0;

    // i2c-ng enters INVALID_STATE after any NACK; recover by reinitialising Wire.
    // Shared by both failure paths below. 200 ms debounce avoids tight reset loops.
    auto on_error = [&]() {
        s_touch_state = LV_INDEV_STATE_REL;
        if (!s_err_since_ms) s_err_since_ms = millis();
        if (millis() - s_err_since_ms >= 200) {
            Serial.println("display: I2C bus recovery (bit-bang SCL)");
            i2c_recover_bus();
            s_err_since_ms = 0;
        }
    };

    Wire.beginTransmission(i2c_addr::FT3168);
    Wire.write(0x02);
    if (Wire.endTransmission(true) != 0) {
        on_error();
        return;
    }

    Wire.requestFrom((int)i2c_addr::FT3168, 5);
    if (Wire.available() < 5) {
        // Drain any partial bytes — leaving them unread corrupts i2c-ng state.
        while (Wire.available()) Wire.read();
        on_error();
        return;
    }

    s_err_since_ms = 0;
    uint8_t count = Wire.read() & 0x0F;
    uint8_t xh = Wire.read(), xl = Wire.read();
    uint8_t yh = Wire.read(), yl = Wire.read();
    if (count > 0) {
        s_touch_pt.x  = (int16_t)(((xh & 0x0F) << 8) | xl);
        s_touch_pt.y  = (int16_t)(((yh & 0x0F) << 8) | yl);
        s_touch_state = LV_INDEV_STATE_PR;
    } else {
        s_touch_state = LV_INDEV_STATE_REL;
    }
}

// Called from main loop at ~5 Hz — refreshes all visible label/widget values.
void display_update()
{
    if (!s_ready) return;
    if (!lvgl_lock(50)) return;

    // ── Shared readings ───────────────────────────────────────────────────────
    bool     ok   = elrs_link_ok();
    unsigned lq   = (unsigned)elrs_link_quality();
    unsigned rssi = (unsigned)elrs_rssi();
    float    v    = power_voltage_v();
    float    amps = power_current_a();
    float    temp = (float)temperatureRead();

    // Session / link duration tracking
    if (!s_session_start_ms) s_session_start_ms = millis();
    if (ok && !s_was_linked) s_link_start_ms = millis();
    s_was_linked = ok;
    unsigned long dur_s = ok ? (millis() - s_link_start_ms) / 1000 : 0;
    float sess_h = (millis() - s_session_start_ms) / 3600000.0f;
    float avg_a  = (sess_h > 0.001f) ? (power_mah_used() / 1000.0f / sess_h) : 0.0f;

    lv_color_t link_c  = ok ? color_lq(lq)   : lv_palette_main(LV_PALETTE_RED);
    lv_color_t rssi_c  = ok ? color_rssi(rssi): lv_palette_main(LV_PALETTE_RED);
    lv_color_t batt_c  = color_batt(v);
    lv_color_t temp_c  = color_temp(temp);
    int        pct     = batt_pct(v);

    float rud = servos_get_commanded(pwm_ch::RUDDER);
    float sai = servos_get_commanded(pwm_ch::SAIL_WINCH);
    float thr = servos_get_commanded(pwm_ch::MOTOR_ESC);
    float rol = imu_roll_deg();
    float pit = imu_pitch_deg();
#ifdef GPS_ENABLED
    float    spd_kmh = gps_speed_kmh();
    bool     gps_fix = gps_has_fix();
    lv_color_t spd_c = gps_fix ? lv_color_white() : lv_color_make(120, 120, 120);
#endif

    // ── Screen 0: WiFi / ELRS control ────────────────────────────────────────
    {
        bool wifi = (wifi_ctrl_mode() == CtrlMode::WIFI);

        lv_label_set_text(s0_lbl_mode, wifi ? "WIFI AP" : "ELRS CTRL");
        lv_obj_set_style_text_color(s0_lbl_mode,
            wifi ? lv_palette_main(LV_PALETTE_TEAL) : lv_palette_main(LV_PALETTE_BLUE),
            LV_PART_MAIN);

        if (wifi) {
            lv_label_set_text(s0_lbl_info1, "SSID: " WIFI_AP_SSID);
            lv_label_set_text(s0_lbl_info2, "IP:   192.168.4.1");
            lv_label_set_text_fmt(s0_lbl_info3, "Clients:  %u", (unsigned)wifi_ctrl_clients());
            unsigned long age_ms = millis() - wifi_ctrl_last_cmd_ms();
            if (age_ms > 9999) age_ms = 9999;
            lv_label_set_text_fmt(s0_lbl_info4, "Last cmd: %lu ms ago", age_ms);
            lv_obj_set_style_text_color(s0_lbl_info4,
                wifi_ctrl_timed_out() ? lv_palette_main(LV_PALETTE_RED) : lv_color_white(),
                LV_PART_MAIN);
            lv_label_set_text(s0_btn_lbl, "Enable ELRS");
            lv_obj_set_style_bg_color(s0_btn, lv_color_make(13, 71, 161), LV_PART_MAIN);
        } else {
            lv_label_set_text_fmt(s0_lbl_info1, "LQ:   %u%%", lq);
            lv_obj_set_style_text_color(s0_lbl_info1, link_c, LV_PART_MAIN);
            lv_label_set_text_fmt(s0_lbl_info2, "RSSI: -%u dBm", rssi);
            lv_obj_set_style_text_color(s0_lbl_info2, rssi_c, LV_PART_MAIN);
            lv_label_set_text(s0_lbl_info3, ok ? "Link:  OK" : "Link:  NO SIGNAL");
            lv_obj_set_style_text_color(s0_lbl_info3, link_c, LV_PART_MAIN);
            lv_label_set_text(s0_lbl_info4, "Failsafe: Phase 3");
            lv_obj_set_style_text_color(s0_lbl_info4, lv_color_make(120,120,120), LV_PART_MAIN);
            lv_label_set_text(s0_btn_lbl, "Enable WiFi");
            lv_obj_set_style_bg_color(s0_btn, lv_color_make(230, 81, 0), LV_PART_MAIN);
        }
    }

    // ── Screen 1: main overview ───────────────────────────────────────────────
    lv_label_set_text_fmt(s1_lbl_link, "Link: %s  LQ: %u%%", ok ? "OK  " : "FAIL", lq);
    lv_obj_set_style_text_color(s1_lbl_link, link_c, LV_PART_MAIN);
    lv_label_set_text_fmt(s1_lbl_rssi, "RSSI: -%u dBm", rssi);
    lv_obj_set_style_text_color(s1_lbl_rssi, rssi_c, LV_PART_MAIN);
    lv_label_set_text_fmt(s1_lbl_batt, "%.2f V   %.2f A", v, amps);
    lv_obj_set_style_text_color(s1_lbl_batt, batt_c, LV_PART_MAIN);
    lv_label_set_text_fmt(s1_lbl_rudder, "Rudder:   %+.3f", rud);
    lv_label_set_text_fmt(s1_lbl_sail,   "Sail:      %.3f", sai);
    lv_label_set_text_fmt(s1_lbl_thr,    "Throttle: %+.3f", thr);
    lv_label_set_text_fmt(s1_lbl_temp, "MCU Temp: %.1f\xC2\xB0""C", temp);
    lv_obj_set_style_text_color(s1_lbl_temp, temp_c, LV_PART_MAIN);
    if (imu_is_capsized()) {
        lv_label_set_text(s1_lbl_status, "!! CAPSIZED !!");
        lv_obj_set_style_text_color(s1_lbl_status, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    } else if (bilge_water_detected()) {
        lv_label_set_text(s1_lbl_status, "BILGE WET");
        lv_obj_set_style_text_color(s1_lbl_status, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    } else {
        lv_label_set_text(s1_lbl_status, "Systems OK");
        lv_obj_set_style_text_color(s1_lbl_status, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }

    // ── Screen 2: link detail ─────────────────────────────────────────────────
    lv_obj_set_style_bg_color(s2_dot, link_c, LV_PART_MAIN);
    lv_label_set_text(s2_lbl_status, ok ? "CONNECTED" : "NO LINK");
    lv_obj_set_style_text_color(s2_lbl_status, link_c, LV_PART_MAIN);
    lv_arc_set_value(s2_arc_lq, (int)lq);
    lv_obj_set_style_arc_color(s2_arc_lq, ok ? color_lq(lq) : lv_palette_main(LV_PALETTE_RED),
                                LV_PART_INDICATOR);
    lv_label_set_text_fmt(s2_lbl_lq_pct, "%u%%", lq);
    lv_obj_set_style_text_color(s2_lbl_lq_pct, link_c, LV_PART_MAIN);
    lv_label_set_text_fmt(s2_lbl_rssi, "RSSI:  -%u dBm", rssi);
    lv_obj_set_style_text_color(s2_lbl_rssi, rssi_c, LV_PART_MAIN);
    lv_label_set_text_fmt(s2_lbl_dur, "Duration: %lu:%02lu:%02lu",
                          dur_s / 3600, (dur_s % 3600) / 60, dur_s % 60);

    // ── Screen 3: battery detail ──────────────────────────────────────────────
    lv_label_set_text_fmt(s3_lbl_volt, "%.2f V", v);
    lv_obj_set_style_text_color(s3_lbl_volt, batt_c, LV_PART_MAIN);
    lv_bar_set_value(s3_bar_volt, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s3_bar_volt, batt_c, LV_PART_INDICATOR);
    lv_label_set_text_fmt(s3_lbl_pct, "%d%%  charged", pct);
    lv_obj_set_style_text_color(s3_lbl_pct, batt_c, LV_PART_MAIN);
    lv_label_set_text_fmt(s3_lbl_curr, "Current:   %.2f A", amps);
    lv_obj_set_style_text_color(s3_lbl_curr, amps > 10.0f ?
                                 lv_palette_main(LV_PALETTE_RED) : lv_color_white(), LV_PART_MAIN);
    lv_label_set_text_fmt(s3_lbl_avg,  "Avg draw:  %.2f A", avg_a);
    lv_label_set_text_fmt(s3_lbl_mah,  "Used:      %.0f mAh", power_mah_used());
    lv_label_set_text_fmt(s3_lbl_temp, "MCU Temp: %.1f\xC2\xB0""C", temp);
    lv_obj_set_style_text_color(s3_lbl_temp, temp_c, LV_PART_MAIN);

    // ── Screen 4: controls ────────────────────────────────────────────────────
    lv_bar_set_value(s4_bar_rudder, (int)(rud * 100.0f), LV_ANIM_OFF);
    lv_label_set_text_fmt(s4_lbl_rudder, "%+.3f", rud);
    lv_bar_set_value(s4_bar_sail,   (int)(sai * 100.0f), LV_ANIM_OFF);
    lv_label_set_text_fmt(s4_lbl_sail, "%.3f", sai);
    lv_bar_set_value(s4_bar_thr,    (int)(thr * 100.0f), LV_ANIM_OFF);
    lv_label_set_text_fmt(s4_lbl_thr, "%+.3f", thr);

    // ── Screen 5: attitude ────────────────────────────────────────────────────
    lv_bar_set_value(s5_bar_roll,  (int)rol, LV_ANIM_OFF);
    lv_label_set_text_fmt(s5_lbl_roll, "%+.1f\xC2\xB0", rol);
    lv_obj_set_style_text_color(s5_lbl_roll, color_angle(rol), LV_PART_MAIN);
    lv_bar_set_value(s5_bar_pitch, (int)pit, LV_ANIM_OFF);
    lv_label_set_text_fmt(s5_lbl_pitch, "%+.1f\xC2\xB0", pit);
    lv_obj_set_style_text_color(s5_lbl_pitch, color_angle(pit), LV_PART_MAIN);

#ifdef COMPASS_ENABLED
    {
        float hdg = compass_heading_deg();
        const char *card = hdg < 22.5f || hdg >= 337.5f ? "N"  :
                           hdg < 67.5f                  ? "NE" :
                           hdg < 112.5f                 ? "E"  :
                           hdg < 157.5f                 ? "SE" :
                           hdg < 202.5f                 ? "S"  :
                           hdg < 247.5f                 ? "SW" :
                           hdg < 292.5f                 ? "W"  : "NW";
        lv_label_set_text_fmt(s5_lbl_heading, "%.0f\xC2\xB0  %s", hdg, card);
    }
#endif
#ifdef GPS_ENABLED
    lv_label_set_text_fmt(s5_lbl_speed, "%.1f km/h", spd_kmh);
    lv_obj_set_style_text_color(s5_lbl_speed, spd_c, LV_PART_MAIN);
#endif

    // ── Screen 6: devices ─────────────────────────────────────────────────────
    for (uint8_t i = 0; i < DEV_COUNT; i++) {
        bool ok = diag_ok((DevId)i);
        lv_color_t sc = ok ? lv_palette_main(LV_PALETTE_GREEN)
                           : lv_palette_main(LV_PALETTE_RED);
        lv_obj_set_style_bg_color(s6_dot[i], sc, LV_PART_MAIN);
        lv_label_set_text(s6_lbl_status[i], ok ? "OK" : "ABSENT");
        lv_obj_set_style_text_color(s6_lbl_status[i], sc, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s6_btn[i],
            ok ? lv_color_make(55, 55, 55) : lv_color_make(183, 28, 28),
            LV_PART_MAIN);
    }

    lvgl_unlock();
}
