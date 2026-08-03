#include <stddef.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "newhaven_display.h"

static const char *TAG = "main";

extern const uint8_t rpm_gauge_rgb565_start[] asm("_binary_rpm_gauge_rgb565_start");
extern const uint8_t rpm_gauge_rgb565_end[] asm("_binary_rpm_gauge_rgb565_end");

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Newhaven NHD-2.1-480480AF-ASXP bring-up");

    newhaven_display_t display;
    ESP_ERROR_CHECK(newhaven_display_init(&display));

    const size_t rpm_gauge_size = rpm_gauge_rgb565_end - rpm_gauge_rgb565_start;
    ESP_LOGI(TAG, "Rendering embedded RPM gauge background (%u bytes)", (unsigned int)rpm_gauge_size);
    ESP_ERROR_CHECK(newhaven_display_draw_rgb565(&display, rpm_gauge_rgb565_start, rpm_gauge_size));
    ESP_LOGI(TAG, "RPM gauge background rendered");
}
