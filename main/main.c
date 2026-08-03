#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "newhaven_display.h"

static const char *TAG = "main";

static const char *newhaven_test_pattern_name(newhaven_test_pattern_t pattern)
{
    switch (pattern) {
    case NEWHAVEN_TEST_PATTERN_BLACK:
        return "black";
    case NEWHAVEN_TEST_PATTERN_WHITE:
        return "white";
    case NEWHAVEN_TEST_PATTERN_RED:
        return "red";
    case NEWHAVEN_TEST_PATTERN_GREEN:
        return "green";
    case NEWHAVEN_TEST_PATTERN_BLUE:
        return "blue";
    case NEWHAVEN_TEST_PATTERN_HORIZONTAL_BARS:
        return "horizontal color bars";
    case NEWHAVEN_TEST_PATTERN_BORDER:
        return "one-pixel border";
    case NEWHAVEN_TEST_PATTERN_CROSSHAIR:
        return "centered crosshair";
    default:
        return "unknown";
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Newhaven NHD-2.1-480480AF-ASXP bring-up");

    newhaven_display_t display;
    ESP_ERROR_CHECK(newhaven_display_init(&display));

    static const newhaven_test_pattern_t test_patterns[] = {
        NEWHAVEN_TEST_PATTERN_BLACK,
        NEWHAVEN_TEST_PATTERN_WHITE,
        NEWHAVEN_TEST_PATTERN_RED,
        NEWHAVEN_TEST_PATTERN_GREEN,
        NEWHAVEN_TEST_PATTERN_BLUE,
        NEWHAVEN_TEST_PATTERN_HORIZONTAL_BARS,
        NEWHAVEN_TEST_PATTERN_BORDER,
        NEWHAVEN_TEST_PATTERN_CROSSHAIR,
    };

    for (size_t index = 0; index < sizeof(test_patterns) / sizeof(test_patterns[0]); index++) {
        ESP_LOGI(TAG, "Displaying %s", newhaven_test_pattern_name(test_patterns[index]));
        ESP_ERROR_CHECK(newhaven_display_draw_test_pattern(&display, test_patterns[index]));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGI(TAG, "Test-pattern sequence complete; repeating");
    while (true) {
        for (size_t index = 0; index < sizeof(test_patterns) / sizeof(test_patterns[0]); index++) {
            ESP_LOGI(TAG, "Displaying %s", newhaven_test_pattern_name(test_patterns[index]));
            ESP_ERROR_CHECK(newhaven_display_draw_test_pattern(&display, test_patterns[index]));
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}
