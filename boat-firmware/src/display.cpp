#include "display.h"
#include "config.h"
#include "esp_lcd_sh8601.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "display";

#define LCD_HOST  SPI2_HOST
#define LCD_H_RES 280
#define LCD_V_RES 456
#define LCD_X_GAP 20   // panel column offset (from Waveshare schematic)

// Init sequence from Waveshare 06_LVGL_Test/lcd_bsp.c
static const sh8601_lcd_init_cmd_t s_init_cmds[] = {
    {0x11, nullptr,          0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, nullptr,          0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static esp_lcd_panel_handle_t s_panel = NULL;

void display_init()
{
    // ── SPI bus (QSPI, 4 data lines) ────────────────────────────────────────
    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_DISP_CLK,
        PIN_DISP_D0, PIN_DISP_D1, PIN_DISP_D2, PIN_DISP_D3,
        LCD_H_RES * 40 * sizeof(uint16_t)
    );
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // ── QSPI panel IO ────────────────────────────────────────────────────────
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_DISP_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // ── SH8601 panel ─────────────────────────────────────────────────────────
    sh8601_vendor_config_t vendor_config = {
        .init_cmds      = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_DISP_RST;
    panel_config.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config  = &vendor_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // X offset: 20 column gap in the physical panel
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, 0));

    ESP_LOGI(TAG, "panel ready — flooding bright blue");

    // ── Flood-fill bright blue ────────────────────────────────────────────────
    const int strip_rows = 40;
    uint16_t *strip = (uint16_t *)heap_caps_malloc(
        LCD_H_RES * strip_rows * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!strip) { ESP_LOGE(TAG, "strip malloc failed"); return; }

    for (int i = 0; i < LCD_H_RES * strip_rows; i++) strip[i] = 0x001F; // bright blue RGB565

    for (int y = 0; y < LCD_V_RES; y += strip_rows) {
        int h = (y + strip_rows <= LCD_V_RES) ? strip_rows : (LCD_V_RES - y);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + h, strip);
    }
    heap_caps_free(strip);

    ESP_LOGI(TAG, "display ready");
}

void display_update()
{
    // Telemetry rendering via LVGL added once sensors are wired.
}
