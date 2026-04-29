#include "display.h"
#include "config.h"
#include "elrs.h"
#include "power.h"
#include "servos.h"

#include <Arduino.h>
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
// Physical column offset of the active area within the driver's address space.
// Applied in the LVGL flush callback, matching Waveshare 06_LVGL_Test/lcd_bsp.c.
#define LCD_X_OFFSET  0x14

// ── LVGL task parameters ──────────────────────────────────────────────────────
#define LVGL_BUF_ROWS      (LCD_V_RES / 4)   // 114 rows — double-buffered
#define LVGL_TICK_MS       2
#define LVGL_TASK_STACK    (4 * 1024)
#define LVGL_TASK_PRIORITY 2

// ── Panel init sequence (verbatim from Waveshare 06_LVGL_Test/lcd_bsp.c) ─────
// Using named arrays instead of compound literals to stay in standard C++.
static const uint8_t s_c4[]      = {0x80};
static const uint8_t s_35[]      = {0x00};
static const uint8_t s_53[]      = {0x20};
static const uint8_t s_63[]      = {0xFF};
static const uint8_t s_51_dim[]  = {0x00};
static const uint8_t s_51_brt[]  = {0xFF};

static const sh8601_lcd_init_cmd_t s_init_cmds[] = {
    // {cmd, data, data_bytes, delay_ms}
    {0x11, nullptr,   0, 80},   // sleep out
    {0xC4, s_c4,      1,  0},   // AMOLED control
    {0x35, s_35,      1,  0},   // tearing effect line on
    {0x53, s_53,      1,  1},   // write CTRL display
    {0x63, s_63,      1,  1},   // HBM brightness max
    {0x51, s_51_dim,  1,  1},   // brightness → 0 initially
    {0x29, nullptr,   0, 10},   // display on
    {0x51, s_51_brt,  1,  0},   // brightness → max
};

// ── Module state ──────────────────────────────────────────────────────────────
static SemaphoreHandle_t s_mux   = nullptr;
static lv_disp_drv_t     s_drv;
static bool              s_ready = false;

// Telemetry label handles (updated by display_update() from the main task)
static lv_obj_t *s_lbl_link   = nullptr;
static lv_obj_t *s_lbl_rssi   = nullptr;
static lv_obj_t *s_lbl_batt   = nullptr;
static lv_obj_t *s_lbl_rudder = nullptr;
static lv_obj_t *s_lbl_sail   = nullptr;
static lv_obj_t *s_lbl_esc    = nullptr;
static lv_obj_t *s_lbl_temp   = nullptr;

// ── LVGL / panel callbacks ────────────────────────────────────────────────────

// Called from DMA ISR when a pixel transfer completes — signals LVGL to flush next.
static bool flush_ready_cb(esp_lcd_panel_io_handle_t,
                            esp_lcd_panel_io_event_data_t *, void *user_ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t *)user_ctx);
    return false;
}

// Called by LVGL to push a rectangular region of pixels to the display.
// Adds the 20-column physical offset (0x14) required by this panel's address map.
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel,
        area->x1 + LCD_X_OFFSET, area->y1,
        area->x2 + LCD_X_OFFSET + 1, area->y2 + 1,
        colors);
}

// Rounds flush area coordinates to even/odd boundaries (from Waveshare demo).
static void rounder_cb(lv_disp_drv_t *, lv_area_t *a)
{
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

static void tick_cb(void *)
{
    lv_tick_inc(LVGL_TICK_MS);
}

// ── LVGL render task (runs independently at higher priority than loop()) ──────
static void lvgl_task(void *)
{
    for (;;) {
        if (xSemaphoreTake(s_mux, portMAX_DELAY) == pdTRUE) {
            uint32_t delay_ms = lv_timer_handler();
            xSemaphoreGive(s_mux);
            delay_ms = delay_ms > 500 ? 500 : (delay_ms < 1 ? 1 : delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
}

// ── Mutex helpers used by display_update() ────────────────────────────────────
static bool lvgl_lock(int timeout_ms)
{
    TickType_t t = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_mux, t) == pdTRUE;
}

static void lvgl_unlock()
{
    xSemaphoreGive(s_mux);
}

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

// ── Build the telemetry screen ────────────────────────────────────────────────
static lv_obj_t *make_label(lv_obj_t *parent, lv_coord_t y, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_pos(lbl, 10, y);
    return lbl;
}

// Bare rectangle — used for separator lines and the title accent strip.
static lv_obj_t *make_rect(lv_obj_t *parent,
                            lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h,
                            lv_color_t color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// Section label ("LINK", "BATTERY" …) in cyan Montserrat-14 + 1px separator.
static void make_section(lv_obj_t *parent, lv_coord_t y, const char *name)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, lv_palette_main(LV_PALETTE_CYAN), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 10, y);
    make_rect(parent, 10, y + 18, LCD_H_RES - 20, 1,
              lv_palette_darken(LV_PALETTE_TEAL, 1));
}

static void build_screen()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // ── Title bar ──────────────────────────────────────────────────────────────
    // Deep ocean teal panel spanning the full display width.
    lv_obj_t *hdr = make_rect(scr, 0, 0, LCD_H_RES, 64,
                               lv_color_make(0, 77, 64));   // Material Teal 900

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "RC Sailboat");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -9);

    lv_obj_t *sub = lv_label_create(hdr);
    lv_label_set_text(sub, "~ telemetry ~");
    lv_obj_set_style_text_color(sub, lv_palette_lighten(LV_PALETTE_TEAL, 3), LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 15);

    // Bright teal accent line under the title bar.
    make_rect(scr, 0, 64, LCD_H_RES, 2, lv_palette_main(LV_PALETTE_TEAL));

    // ── LINK ───────────────────────────────────────────────────────────────────
    make_section(scr, 74, "LINK");
    s_lbl_link = make_label(scr, 100, "Link: --  LQ: --%");
    s_lbl_rssi = make_label(scr, 126, "RSSI: -- dBm");

    // ── BATTERY ────────────────────────────────────────────────────────────────
    make_section(scr, 160, "BATTERY");
    s_lbl_batt = make_label(scr, 186, "--.-- V   --.-- A");

    // ── SERVOS ─────────────────────────────────────────────────────────────────
    make_section(scr, 218, "SERVOS");
    s_lbl_rudder = make_label(scr, 244, "Rudder:  +0.000");
    s_lbl_sail   = make_label(scr, 270, "Sail:     0.000");
    s_lbl_esc    = make_label(scr, 296, "ESC:     +0.000");

    // ── SYSTEM ─────────────────────────────────────────────────────────────────
    make_section(scr, 330, "SYSTEM");
    s_lbl_temp = make_label(scr, 356, "MCU Temp: --\xC2\xB0""C");
}

// ── Public API ────────────────────────────────────────────────────────────────

void display_init()
{
    // SPI bus — QSPI, 4 data lines to CO5300 / SH8601 AMOLED
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num    = (int)pins_unused::OLED_CLK;
    buscfg.data0_io_num   = (int)pins_unused::OLED_D0;
    buscfg.data1_io_num   = (int)pins_unused::OLED_D1;
    buscfg.data2_io_num   = (int)pins_unused::OLED_D2;
    buscfg.data3_io_num   = (int)pins_unused::OLED_D3;
    buscfg.max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Panel IO — QSPI, 32-bit command, DMA flush completion signals LVGL
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num         = (int)pins_unused::OLED_CS;
    io_cfg.dc_gpio_num         = -1;
    io_cfg.spi_mode            = 0;
    io_cfg.pclk_hz             = 40 * 1000 * 1000;
    io_cfg.trans_queue_depth   = 10;
    io_cfg.on_color_trans_done = flush_ready_cb;
    io_cfg.user_ctx            = &s_drv;   // passed to flush_ready_cb
    io_cfg.lcd_cmd_bits        = 32;
    io_cfg.lcd_param_bits      = 8;
    io_cfg.flags.quad_mode     = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    // SH8601 / CO5300 panel device
    sh8601_vendor_config_t vendor = {};
    vendor.init_cmds            = s_init_cmds;
    vendor.init_cmds_size       = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]);
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

    // LVGL init
    lv_init();

    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_ROWS * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        LCD_H_RES * LVGL_BUF_ROWS * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 && buf2);

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_ROWS);

    lv_disp_drv_init(&s_drv);
    s_drv.hor_res    = LCD_H_RES;
    s_drv.ver_res    = LCD_V_RES;
    s_drv.flush_cb   = flush_cb;
    s_drv.rounder_cb = rounder_cb;
    s_drv.draw_buf   = &draw_buf;
    s_drv.user_data  = panel;   // retrieved in flush_cb
    lv_disp_drv_register(&s_drv);

    // Tick timer — calls lv_tick_inc() every 2 ms via esp_timer
    esp_timer_create_args_t tick_args = {};
    tick_args.callback = tick_cb;
    tick_args.name     = "lvgl_tick";
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    s_mux = xSemaphoreCreateMutex();
    assert(s_mux);

    build_screen();

    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK, nullptr,
                LVGL_TASK_PRIORITY, nullptr);

    s_ready = true;
    ESP_LOGI(TAG, "display ready — 280x456 AMOLED");
}

// Called by the main loop at ~5 Hz to refresh all telemetry labels.
// Takes the LVGL mutex so it is safe to run concurrently with the LVGL task.
void display_update()
{
    if (!s_ready) return;
    if (!lvgl_lock(50)) return;   // skip update if LVGL is busy

    bool     ok   = elrs_link_ok();
    unsigned lq   = (unsigned)elrs_link_quality();
    unsigned rssi = (unsigned)elrs_rssi();
    float    v    = power_voltage_v();
    float    temp = (float)temperatureRead();

    lv_label_set_text_fmt(s_lbl_link, "Link: %s  LQ: %u%%", ok ? "OK  " : "FAIL", lq);
    lv_obj_set_style_text_color(s_lbl_link,
        ok ? color_lq(lq) : lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_label_set_text_fmt(s_lbl_rssi, "RSSI: -%u dBm", rssi);
    lv_obj_set_style_text_color(s_lbl_rssi,
        ok ? color_rssi(rssi) : lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_label_set_text_fmt(s_lbl_batt, "%.2f V   %.2f A", v, power_current_a());
    lv_obj_set_style_text_color(s_lbl_batt, color_batt(v), LV_PART_MAIN);

    lv_label_set_text_fmt(s_lbl_rudder, "Rudder: %+.3f", servos_get_commanded(pwm_ch::RUDDER));
    lv_label_set_text_fmt(s_lbl_sail,   "Sail:   %.3f",  servos_get_commanded(pwm_ch::SAIL_WINCH));
    lv_label_set_text_fmt(s_lbl_esc,    "ESC:    %+.3f", servos_get_commanded(pwm_ch::MOTOR_ESC));

    lv_label_set_text_fmt(s_lbl_temp, "MCU Temp: %.1f\xC2\xB0""C", temp);
    lv_obj_set_style_text_color(s_lbl_temp, color_temp(temp), LV_PART_MAIN);

    lvgl_unlock();
}
