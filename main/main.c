#include <stddef.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_config.h"
#include "gauge_renderer.h"
#include "newhaven_display.h"

static const char *TAG = "main";

extern const uint8_t rpm_gauge_rgb565_start[] asm("_binary_rpm_gauge_rgb565_start");
extern const uint8_t rpm_gauge_rgb565_end[] asm("_binary_rpm_gauge_rgb565_end");

#define GAUGE_FRAME_RATE_HZ 30
#define GAUGE_SWEEP_DURATION_MS 3000
#define GAUGE_CYCLE_DURATION_MS (GAUGE_SWEEP_DURATION_MS * 2)
#define GAUGE_LOG_INTERVAL_MS 1000

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Newhaven NHD-2.1-480480AF-ASXP bring-up");

    newhaven_display_t display;
    ESP_ERROR_CHECK(newhaven_display_init(&display));

    const size_t rpm_gauge_size = rpm_gauge_rgb565_end - rpm_gauge_rgb565_start;
    ESP_LOGI(TAG, "Preparing %d framebuffer(s) from the embedded RPM gauge (%u bytes)",
             NEWHAVEN_DISPLAY_FB_COUNT,
             (unsigned int)rpm_gauge_size);
    for (int index = 0; index < NEWHAVEN_DISPLAY_FB_COUNT; index++) {
        void *framebuffer;
        ESP_ERROR_CHECK(newhaven_display_begin_frame(&display, &framebuffer));
        ESP_ERROR_CHECK(newhaven_display_prepare_rgb565(&display,
                                                         rpm_gauge_rgb565_start,
                                                         rpm_gauge_size));
        ESP_ERROR_CHECK(newhaven_display_flush_framebuffer(&display));
    }
    ESP_ERROR_CHECK(gauge_renderer_init(rpm_gauge_rgb565_start, rpm_gauge_size));
    ESP_LOGI(TAG, "Starting double-buffered animated RPM gauge");

    const TickType_t frame_period = pdMS_TO_TICKS(1000 / GAUGE_FRAME_RATE_HZ);
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t animation_start_time = last_wake_time;
    uint32_t last_log_time_ms = 0;

    while (true) {
        const uint32_t elapsed_time_ms =
            (uint32_t)((xTaskGetTickCount() - animation_start_time) * portTICK_PERIOD_MS);
        const uint32_t cycle_time_ms = elapsed_time_ms % GAUGE_CYCLE_DURATION_MS;
        const float cycle_fraction = (float)(cycle_time_ms <= GAUGE_SWEEP_DURATION_MS ?
                                                 cycle_time_ms :
                                                 GAUGE_CYCLE_DURATION_MS - cycle_time_ms) /
                                     (float)GAUGE_SWEEP_DURATION_MS;
        const float target_value = cycle_fraction * 5.0f;

        gauge_renderer_set_value(target_value);
        void *framebuffer;
        ESP_ERROR_CHECK(newhaven_display_begin_frame(&display, &framebuffer));
        ESP_ERROR_CHECK(gauge_renderer_render(framebuffer));
        ESP_ERROR_CHECK(newhaven_display_flush_framebuffer(&display));

        if (elapsed_time_ms - last_log_time_ms >= GAUGE_LOG_INTERVAL_MS) {
            const gauge_state_t *gauge_state = gauge_renderer_get_state();
            ESP_LOGI(TAG, "Gauge target %.2f, displayed %.2f, angle %.1f degrees",
                     gauge_state->target_value,
                     gauge_state->displayed_value,
                     gauge_value_to_angle(gauge_state->displayed_value));
            last_log_time_ms = elapsed_time_ms;
        }

        vTaskDelayUntil(&last_wake_time, frame_period);
    }
}
