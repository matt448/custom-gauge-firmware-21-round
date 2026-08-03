#pragma once

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"

typedef enum {
    NEWHAVEN_TEST_PATTERN_BLACK,
    NEWHAVEN_TEST_PATTERN_WHITE,
    NEWHAVEN_TEST_PATTERN_RED,
    NEWHAVEN_TEST_PATTERN_GREEN,
    NEWHAVEN_TEST_PATTERN_BLUE,
    NEWHAVEN_TEST_PATTERN_HORIZONTAL_BARS,
    NEWHAVEN_TEST_PATTERN_BORDER,
    NEWHAVEN_TEST_PATTERN_CROSSHAIR,
} newhaven_test_pattern_t;

typedef struct {
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_panel_io_handle_t dbi_io;
    esp_lcd_panel_handle_t dpi_panel;
    esp_ldo_channel_handle_t dsi_phy_ldo;
    void *framebuffer;
} newhaven_display_t;

esp_err_t newhaven_display_reset(void);
esp_err_t newhaven_st7701s_init(esp_lcd_panel_handle_t panel);
esp_err_t newhaven_display_init(newhaven_display_t *display);
esp_err_t newhaven_display_draw_test_pattern(newhaven_display_t *display,
                                             newhaven_test_pattern_t pattern);
