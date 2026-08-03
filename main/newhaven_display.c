#include <stdint.h>
#include <string.h>

#include "esp_cache.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_lcd_st7701.h"

#include "display_config.h"
#include "newhaven_display.h"

static const char *TAG = "newhaven_display";

#if NEWHAVEN_DISPLAY_DSI_DATA_LANES != 1
#error "The NHD-2.1-480480AF-ASXP uses only DSI data lane 0."
#endif

#if (NEWHAVEN_DISPLAY_PIXEL_FORMAT != NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888) && \
    (NEWHAVEN_DISPLAY_PIXEL_FORMAT != NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB565)
#error "NEWHAVEN_DISPLAY_PIXEL_FORMAT must be RGB888 or RGB565."
#endif

#if (NEWHAVEN_DISPLAY_COLOR_ORDER != NEWHAVEN_DISPLAY_COLOR_ORDER_RGB) && \
    (NEWHAVEN_DISPLAY_COLOR_ORDER != NEWHAVEN_DISPLAY_COLOR_ORDER_BGR)
#error "NEWHAVEN_DISPLAY_COLOR_ORDER must be RGB or BGR."
#endif

#if (NEWHAVEN_DISPLAY_ROTATION != NEWHAVEN_DISPLAY_ROTATION_0) && \
    (NEWHAVEN_DISPLAY_ROTATION != NEWHAVEN_DISPLAY_ROTATION_90) && \
    (NEWHAVEN_DISPLAY_ROTATION != NEWHAVEN_DISPLAY_ROTATION_180) && \
    (NEWHAVEN_DISPLAY_ROTATION != NEWHAVEN_DISPLAY_ROTATION_270)
#error "NEWHAVEN_DISPLAY_ROTATION must be 0, 90, 180, or 270."
#endif

#if (NEWHAVEN_DISPLAY_FB_COUNT < 1) || (NEWHAVEN_DISPLAY_FB_COUNT > 2)
#error "NEWHAVEN_DISPLAY_FB_COUNT must be 1 or 2."
#endif

#if NEWHAVEN_DISPLAY_PIXEL_FORMAT == NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888
#define NEWHAVEN_DISPLAY_BYTES_PER_PIXEL 3
#define NEWHAVEN_DISPLAY_LCD_COLOR_FORMAT LCD_COLOR_FMT_RGB888
#else
#define NEWHAVEN_DISPLAY_BYTES_PER_PIXEL 2
#define NEWHAVEN_DISPLAY_LCD_COLOR_FORMAT LCD_COLOR_FMT_RGB565
#endif

#define NEWHAVEN_DISPLAY_FRAMEBUFFER_SIZE \
    (NEWHAVEN_DISPLAY_H_RES * NEWHAVEN_DISPLAY_V_RES * NEWHAVEN_DISPLAY_BYTES_PER_PIXEL)

#if NEWHAVEN_DISPLAY_PIXEL_FORMAT == NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888
#define NEWHAVEN_DISPLAY_BITS_PER_PIXEL 24
#else
#define NEWHAVEN_DISPLAY_BITS_PER_PIXEL 16
#endif

#if NEWHAVEN_DISPLAY_COLOR_ORDER == NEWHAVEN_DISPLAY_COLOR_ORDER_RGB
#define NEWHAVEN_DISPLAY_RGB_ELEMENT_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#else
#define NEWHAVEN_DISPLAY_RGB_ELEMENT_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#endif

#define RETURN_ON_ERROR(expression, stage) do { \
    esp_err_t __err = (expression); \
    if (__err != ESP_OK) { \
        ESP_LOGE(TAG, "%s failed: %s", stage, esp_err_to_name(__err)); \
        return __err; \
    } \
} while (0)

static void newhaven_display_map_coordinate(int x, int y, int *mapped_x, int *mapped_y)
{
    switch (NEWHAVEN_DISPLAY_ROTATION) {
    case NEWHAVEN_DISPLAY_ROTATION_0:
        *mapped_x = x;
        *mapped_y = y;
        break;
    case NEWHAVEN_DISPLAY_ROTATION_90:
        *mapped_x = NEWHAVEN_DISPLAY_H_RES - 1 - y;
        *mapped_y = x;
        break;
    case NEWHAVEN_DISPLAY_ROTATION_180:
        *mapped_x = NEWHAVEN_DISPLAY_H_RES - 1 - x;
        *mapped_y = NEWHAVEN_DISPLAY_V_RES - 1 - y;
        break;
    case NEWHAVEN_DISPLAY_ROTATION_270:
        *mapped_x = y;
        *mapped_y = NEWHAVEN_DISPLAY_V_RES - 1 - x;
        break;
    default:
        *mapped_x = x;
        *mapped_y = y;
        break;
    }
}

static void newhaven_display_put_pixel(void *framebuffer, int x, int y, uint32_t rgb)
{
    int mapped_x;
    int mapped_y;
    newhaven_display_map_coordinate(x, y, &mapped_x, &mapped_y);

#if NEWHAVEN_DISPLAY_PIXEL_FORMAT == NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888
    uint8_t *pixel = (uint8_t *)framebuffer +
                     ((mapped_y * NEWHAVEN_DISPLAY_H_RES + mapped_x) * NEWHAVEN_DISPLAY_BYTES_PER_PIXEL);
    uint8_t red = (rgb >> 16) & 0xff;
    uint8_t green = (rgb >> 8) & 0xff;
    uint8_t blue = rgb & 0xff;
#if NEWHAVEN_DISPLAY_COLOR_ORDER == NEWHAVEN_DISPLAY_COLOR_ORDER_RGB
    pixel[0] = blue;
    pixel[1] = green;
    pixel[2] = red;
#else
    pixel[0] = red;
    pixel[1] = green;
    pixel[2] = blue;
#endif
#else
    uint16_t red = (rgb >> 19) & 0x1f;
    uint16_t green = (rgb >> 10) & 0x3f;
    uint16_t blue = (rgb >> 3) & 0x1f;
#if NEWHAVEN_DISPLAY_COLOR_ORDER == NEWHAVEN_DISPLAY_COLOR_ORDER_BGR
    uint16_t temporary = red;
    red = blue;
    blue = temporary;
#endif
    ((uint16_t *)framebuffer)[mapped_y * NEWHAVEN_DISPLAY_H_RES + mapped_x] =
        (red << 11) | (green << 5) | blue;
#endif
}

esp_err_t newhaven_display_reset(void)
{
#if !NEWHAVEN_DISPLAY_RESET_GPIO_CONFIGURED
    ESP_LOGE(TAG, "RESETX GPIO is not configured; set NEWHAVEN_DISPLAY_RESET_GPIO and NEWHAVEN_DISPLAY_RESET_GPIO_CONFIGURED in display_config.h");
    return ESP_ERR_INVALID_STATE;
#else
    const gpio_num_t reset_gpio = NEWHAVEN_DISPLAY_RESET_GPIO;

    if (!GPIO_IS_VALID_GPIO(reset_gpio)) {
        ESP_LOGE(TAG, "RESETX GPIO is not configured; set NEWHAVEN_DISPLAY_RESET_GPIO in display_config.h");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Configuring and resetting RESETX");
    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << reset_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    RETURN_ON_ERROR(gpio_config(&reset_config), "configure RESETX GPIO");
    RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), "drive RESETX high");
    RETURN_ON_ERROR(gpio_set_level(reset_gpio, 0), "drive RESETX low");
    esp_rom_delay_us(1000);
    RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), "release RESETX");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
#endif
}

/*
 * Exact ST7701S register sequence published by Newhaven for the
 * NHD-2.1-480480AF-ASXP:
 * https://support.newhavendisplay.com/hc/en-us/articles/30069060235927-NHD-2-1-480480AF-ASXP-with-SSD1963-Controller-with-Arduino
 *
 * The reference uses a 3-wire SPI command interface and an SSD1963 for pixel
 * data. This project sends only the same panel-register commands through the
 * native MIPI DSI DBI command channel; it does not use the SSD1963 code or
 * any Arduino-specific transport, GPIO, SD-card, or image-loading code.
 */
static const st7701_lcd_init_cmd_t newhaven_st7701s_init_cmds[] = {
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t []){0x08}, 1, 0},
    {0x3A, (uint8_t []){0x6F}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t []){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t []){0x09, 0x05}, 2, 0},
    {0xC2, (uint8_t []){0x07, 0x02}, 2, 0},
    {0xC6, (uint8_t []){0x21}, 1, 0},
    {0xCC, (uint8_t []){0x30}, 1, 0},
    {0xB0, (uint8_t []){0xC0, 0x54, 0x5C, 0x0D, 0x51, 0x06, 0x09, 0x08, 0x07, 0x24, 0x03, 0x11, 0x0F, 0xAC, 0xB5, 0x7F}, 16, 0},
    {0xB1, (uint8_t []){0xC0, 0x54, 0x5C, 0x0E, 0x11, 0x07, 0x0A, 0x09, 0x08, 0x24, 0x04, 0x51, 0x10, 0xAD, 0x75, 0x7F}, 16, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t []){0x7D}, 1, 0},
    {0xB1, (uint8_t []){0x33}, 1, 0},
    {0xB2, (uint8_t []){0x87}, 1, 0},
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x45}, 1, 0},
    {0xB7, (uint8_t []){0x87}, 1, 0},
    {0xB8, (uint8_t []){0x33}, 1, 0},
    {0xB9, (uint8_t []){0x10}, 1, 0},
    {0xBB, (uint8_t []){0x03}, 1, 0},
    {0xC0, (uint8_t []){0x03}, 1, 0},
    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 0},
    {0xD0, (uint8_t []){0x88}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xE0, (uint8_t []){0x00, 0x18, 0x00, 0x00, 0x00, 0x20}, 6, 0},
    {0xE1, (uint8_t []){0x05, 0xA0, 0x00, 0xA0, 0x04, 0x0A, 0x00, 0xA0, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t []){0x11, 0x11, 0x44, 0x44, 0xEA, 0xA0, 0x00, 0x00, 0xE9, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x06, 0xE5, 0xD8, 0xA0, 0x08, 0xE7, 0xD8, 0xA0, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x05, 0xE4, 0xD8, 0xA0, 0x07, 0xE6, 0xD8, 0xA0, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t []){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x10}, 7, 0},
    {0xEC, (uint8_t []){0x3D, 0x02, 0x00}, 3, 0},
    {0xED, (uint8_t []){0x20, 0x76, 0x54, 0x98, 0xBA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAB, 0x89, 0x45, 0x67, 0x02}, 16, 0},
    {0xEF, (uint8_t []){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE8, (uint8_t []){0x00, 0x0E}, 2, 0},
    {0x11, NULL, 0, 120},
    {0xE8, (uint8_t []){0x00, 0x0C}, 2, 10},
    {0xE8, (uint8_t []){0x00, 0x00}, 2, 0},
    {0xE6, (uint8_t []){0x16, 0x7C}, 2, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x29, NULL, 0, 120},
    {0x36, (uint8_t []){0x00}, 1, 0},
};

esp_err_t newhaven_st7701s_init(esp_lcd_panel_handle_t panel)
{
    if (panel == NULL) {
        ESP_LOGE(TAG, "ST7701S panel is unavailable");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending Newhaven NHD-2.1-480480AF-ASXP ST7701S initialization sequence");
    RETURN_ON_ERROR(esp_lcd_panel_init(panel), "send ST7701S initialization commands");
    RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), "send Display On command");
    return ESP_OK;
}

esp_err_t newhaven_display_init(newhaven_display_t *display)
{
    if (display == NULL) {
        ESP_LOGE(TAG, "display handle is null");
        return ESP_ERR_INVALID_ARG;
    }
    memset(display, 0, sizeof(*display));

#if NEWHAVEN_DISPLAY_ENABLE_DSI_PHY_LDO
    ESP_LOGI(TAG, "Enabling MIPI DSI PHY internal LDO");
    const esp_ldo_channel_config_t ldo_config = {
        .chan_id = NEWHAVEN_DISPLAY_DSI_PHY_LDO_CHANNEL,
        .voltage_mv = NEWHAVEN_DISPLAY_DSI_PHY_LDO_MV,
    };
    RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_config, &display->dsi_phy_ldo),
                    "enable MIPI DSI PHY LDO");
#endif

    RETURN_ON_ERROR(newhaven_display_reset(), "hardware reset");

    ESP_LOGI(TAG, "Creating one-lane MIPI DSI bus on data lane 0");
    const esp_lcd_dsi_bus_config_t dsi_bus_config = {
        .bus_id = NEWHAVEN_DISPLAY_DSI_BUS_ID,
        .num_data_lanes = NEWHAVEN_DISPLAY_DSI_DATA_LANES,
        /*
         * ESP32-P4 v1.x uses the legacy DSI PHY implementation, which does
         * not accept the v3+ XTAL default. PLL_F20M is ESP-IDF's documented
         * legacy default and is valid on the connected v1.3 board.
         */
        .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY,
        .lane_bit_rate_mbps = NEWHAVEN_DISPLAY_DSI_LANE_BIT_RATE_MBPS,
    };
    RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi_bus_config, &display->dsi_bus), "create MIPI DSI bus");

    ESP_LOGI(TAG, "Creating MIPI DSI command interface");
    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(display->dsi_bus, &dbi_config, &display->dbi_io),
                    "create MIPI DSI command interface");

    ESP_LOGI(TAG, "Creating 480x480 MIPI DSI ST7701S panel with %d framebuffer(s)",
             NEWHAVEN_DISPLAY_FB_COUNT);
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = NEWHAVEN_DISPLAY_PIXEL_CLOCK_MHZ,
        .in_color_format = NEWHAVEN_DISPLAY_LCD_COLOR_FORMAT,
        .out_color_format = NEWHAVEN_DISPLAY_LCD_COLOR_FORMAT,
        .num_fbs = NEWHAVEN_DISPLAY_FB_COUNT,
        .video_timing = {
            .h_size = NEWHAVEN_DISPLAY_H_RES,
            .v_size = NEWHAVEN_DISPLAY_V_RES,
            .hsync_back_porch = NEWHAVEN_DISPLAY_H_BACK_PORCH,
            .hsync_pulse_width = NEWHAVEN_DISPLAY_H_SYNC_PULSE,
            .hsync_front_porch = NEWHAVEN_DISPLAY_H_FRONT_PORCH,
            .vsync_back_porch = NEWHAVEN_DISPLAY_V_BACK_PORCH,
            .vsync_pulse_width = NEWHAVEN_DISPLAY_V_SYNC_PULSE,
            .vsync_front_porch = NEWHAVEN_DISPLAY_V_FRONT_PORCH,
        },
    };
    st7701_vendor_config_t st7701_vendor_config = {
        .init_cmds = newhaven_st7701s_init_cmds,
        .init_cmds_size = sizeof(newhaven_st7701s_init_cmds) / sizeof(newhaven_st7701s_init_cmds[0]),
        .flags.use_mipi_interface = 1,
        .mipi_config = {
            .dsi_bus = display->dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        /*
         * RESETX was already pulsed and held high for 120 ms by
         * newhaven_display_reset(). Passing GPIO_NUM_NC prevents the managed
         * driver from replacing that required reset sequence.
         */
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = NEWHAVEN_DISPLAY_RGB_ELEMENT_ORDER,
        .bits_per_pixel = NEWHAVEN_DISPLAY_BITS_PER_PIXEL,
        .vendor_config = &st7701_vendor_config,
    };
    RETURN_ON_ERROR(esp_lcd_new_panel_st7701(display->dbi_io, &panel_config, &display->dpi_panel),
                    "create MIPI DSI ST7701S panel");
    RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(display->dpi_panel, 1, &display->framebuffer),
                    "get framebuffer");

    ESP_LOGI(TAG, "Initializing ST7701S controller");
    RETURN_ON_ERROR(newhaven_st7701s_init(display->dpi_panel), "initialize ST7701S");
    return ESP_OK;
}

esp_err_t newhaven_display_draw_rgb565(newhaven_display_t *display,
                                       const uint8_t *image,
                                       size_t image_size)
{
    const size_t expected_size = NEWHAVEN_DISPLAY_H_RES * NEWHAVEN_DISPLAY_V_RES * 2;

    if (display == NULL || display->framebuffer == NULL || image == NULL) {
        ESP_LOGE(TAG, "framebuffer is unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    if (image_size != expected_size) {
        ESP_LOGE(TAG, "RGB565 asset has %u bytes; expected %u bytes",
                 (unsigned int)image_size, (unsigned int)expected_size);
        return ESP_ERR_INVALID_SIZE;
    }

    for (int y = 0; y < NEWHAVEN_DISPLAY_V_RES; y++) {
        for (int x = 0; x < NEWHAVEN_DISPLAY_H_RES; x++) {
            const size_t offset = ((size_t)y * NEWHAVEN_DISPLAY_H_RES + x) * 2;
            const uint16_t pixel = ((uint16_t)image[offset] << 8) | image[offset + 1];
            const uint8_t red = (pixel >> 11) & 0x1f;
            const uint8_t green = (pixel >> 5) & 0x3f;
            const uint8_t blue = pixel & 0x1f;
            const uint32_t rgb = ((uint32_t)((red << 3) | (red >> 2)) << 16) |
                                 ((uint32_t)((green << 2) | (green >> 4)) << 8) |
                                 ((uint32_t)((blue << 3) | (blue >> 2)));
            newhaven_display_put_pixel(display->framebuffer, x, y, rgb);
        }
    }

    RETURN_ON_ERROR(esp_cache_msync(display->framebuffer, NEWHAVEN_DISPLAY_FRAMEBUFFER_SIZE,
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED),
                    "write framebuffer cache");
    return ESP_OK;
}
