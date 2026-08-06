#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "display_config.h"
#include "gauge_renderer.h"

#define GAUGE_RENDERER_DEGREES_TO_RADIANS (3.14159265358979323846f / 180.0f)
#define GAUGE_RENDERER_HUB_RADIUS          6
#define GAUGE_RENDERER_BACKGROUND_SIZE     \
    (NEWHAVEN_DISPLAY_H_RES * NEWHAVEN_DISPLAY_V_RES * 2)

typedef void (*gauge_pixel_writer_t)(void *framebuffer, int x, int y, const void *context);

typedef struct {
    void *framebuffer;
    float angle_deg;
    bool has_needle;
} gauge_frame_history_t;

static gauge_state_t s_gauge_state;
static gauge_frame_history_t s_frame_history[NEWHAVEN_DISPLAY_FB_COUNT];
static const uint8_t *s_background_rgb565;
static bool s_initialized;

static float gauge_clamp_value(float value)
{
    if (value < s_gauge_state.min_value) {
        return s_gauge_state.min_value;
    }
    if (value > s_gauge_state.max_value) {
        return s_gauge_state.max_value;
    }
    return value;
}

static void gauge_map_coordinate(int x, int y, int *mapped_x, int *mapped_y)
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

static void gauge_put_pixel(void *framebuffer, int x, int y, uint32_t rgb)
{
    if (x < 0 || x >= NEWHAVEN_DISPLAY_H_RES || y < 0 || y >= NEWHAVEN_DISPLAY_V_RES) {
        return;
    }

    int mapped_x;
    int mapped_y;
    gauge_map_coordinate(x, y, &mapped_x, &mapped_y);

#if NEWHAVEN_DISPLAY_PIXEL_FORMAT == NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888
    uint8_t *pixel = (uint8_t *)framebuffer +
                     ((mapped_y * NEWHAVEN_DISPLAY_H_RES + mapped_x) * 3);
    const uint8_t red = (rgb >> 16) & 0xff;
    const uint8_t green = (rgb >> 8) & 0xff;
    const uint8_t blue = rgb & 0xff;
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
    const uint16_t temporary = red;
    red = blue;
    blue = temporary;
#endif
    ((uint16_t *)framebuffer)[mapped_y * NEWHAVEN_DISPLAY_H_RES + mapped_x] =
        (red << 11) | (green << 5) | blue;
#endif
}

static void gauge_write_solid_pixel(void *framebuffer, int x, int y, const void *context)
{
    gauge_put_pixel(framebuffer, x, y, *(const uint32_t *)context);
}

static void gauge_write_background_pixel(void *framebuffer, int x, int y, const void *context)
{
    if (x < 0 || x >= NEWHAVEN_DISPLAY_H_RES || y < 0 || y >= NEWHAVEN_DISPLAY_V_RES) {
        return;
    }

    const uint8_t *background = context;
    const size_t offset = ((size_t)y * NEWHAVEN_DISPLAY_H_RES + x) * 2;
    const uint16_t pixel = ((uint16_t)background[offset] << 8) | background[offset + 1];
    const uint8_t red = (pixel >> 11) & 0x1f;
    const uint8_t green = (pixel >> 5) & 0x3f;
    const uint8_t blue = pixel & 0x1f;
    const uint32_t rgb = ((uint32_t)((red << 3) | (red >> 2)) << 16) |
                         ((uint32_t)((green << 2) | (green >> 4)) << 8) |
                         ((uint32_t)((blue << 3) | (blue >> 2)));
    gauge_put_pixel(framebuffer, x, y, rgb);
}

static int64_t gauge_edge_function(int start_x, int start_y,
                                   int end_x, int end_y,
                                   int point_x, int point_y)
{
    return (int64_t)(end_x - start_x) * (point_y - start_y) -
           (int64_t)(end_y - start_y) * (point_x - start_x);
}

static void gauge_fill_taper(void *framebuffer,
                              const int vertex_x[4],
                              const int vertex_y[4],
                              gauge_pixel_writer_t writer,
                              const void *context)
{
    int min_x = vertex_x[0];
    int max_x = vertex_x[0];
    int min_y = vertex_y[0];
    int max_y = vertex_y[0];

    for (int index = 1; index < 4; index++) {
        if (vertex_x[index] < min_x) {
            min_x = vertex_x[index];
        }
        if (vertex_x[index] > max_x) {
            max_x = vertex_x[index];
        }
        if (vertex_y[index] < min_y) {
            min_y = vertex_y[index];
        }
        if (vertex_y[index] > max_y) {
            max_y = vertex_y[index];
        }
    }

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            const int64_t edge_0 = gauge_edge_function(vertex_x[0], vertex_y[0],
                                                        vertex_x[1], vertex_y[1], x, y);
            const int64_t edge_1 = gauge_edge_function(vertex_x[1], vertex_y[1],
                                                        vertex_x[2], vertex_y[2], x, y);
            const int64_t edge_2 = gauge_edge_function(vertex_x[2], vertex_y[2],
                                                        vertex_x[3], vertex_y[3], x, y);
            const int64_t edge_3 = gauge_edge_function(vertex_x[3], vertex_y[3],
                                                        vertex_x[0], vertex_y[0], x, y);
            const bool inside_clockwise = edge_0 <= 0 && edge_1 <= 0 &&
                                          edge_2 <= 0 && edge_3 <= 0;
            const bool inside_counterclockwise = edge_0 >= 0 && edge_1 >= 0 &&
                                                 edge_2 >= 0 && edge_3 >= 0;
            if (inside_clockwise || inside_counterclockwise) {
                writer(framebuffer, x, y, context);
            }
        }
    }
}

static void gauge_draw_hub(void *framebuffer,
                           int center_x,
                           int center_y,
                           gauge_pixel_writer_t writer,
                           const void *context)
{
    for (int y = -GAUGE_RENDERER_HUB_RADIUS; y <= GAUGE_RENDERER_HUB_RADIUS; y++) {
        for (int x = -GAUGE_RENDERER_HUB_RADIUS; x <= GAUGE_RENDERER_HUB_RADIUS; x++) {
            if ((x * x) + (y * y) <= GAUGE_RENDERER_HUB_RADIUS * GAUGE_RENDERER_HUB_RADIUS) {
                writer(framebuffer, center_x + x, center_y + y, context);
            }
        }
    }
}

static void gauge_draw_needle_shape(void *framebuffer,
                                    int center_x,
                                    int center_y,
                                    float angle_deg,
                                    int length,
                                    int base_width,
                                    int tip_width,
                                    gauge_pixel_writer_t writer,
                                    const void *context)
{
    const float angle_rad = angle_deg * GAUGE_RENDERER_DEGREES_TO_RADIANS;
    const float cosine = cosf(angle_rad);
    const float sine = sinf(angle_rad);
    const int endpoint_x = center_x + (int)lroundf(cosine * length);
    const int endpoint_y = center_y - (int)lroundf(sine * length);
    const int first_base_offset = -(base_width / 2);
    const int first_tip_offset = -(tip_width / 2);
    const int last_base_offset = first_base_offset + base_width - 1;
    const int last_tip_offset = first_tip_offset + tip_width - 1;
    const int vertex_x[4] = {
        center_x + (int)lroundf(sine * first_base_offset),
        center_x + (int)lroundf(sine * last_base_offset),
        endpoint_x + (int)lroundf(sine * last_tip_offset),
        endpoint_x + (int)lroundf(sine * first_tip_offset),
    };
    const int vertex_y[4] = {
        center_y + (int)lroundf(cosine * first_base_offset),
        center_y + (int)lroundf(cosine * last_base_offset),
        endpoint_y + (int)lroundf(cosine * last_tip_offset),
        endpoint_y + (int)lroundf(cosine * first_tip_offset),
    };

    gauge_fill_taper(framebuffer, vertex_x, vertex_y, writer, context);
    gauge_draw_hub(framebuffer, center_x, center_y, writer, context);
}

static gauge_frame_history_t *gauge_get_frame_history(void *framebuffer)
{
    for (size_t index = 0; index < NEWHAVEN_DISPLAY_FB_COUNT; index++) {
        if (s_frame_history[index].framebuffer == framebuffer) {
            return &s_frame_history[index];
        }
    }
    for (size_t index = 0; index < NEWHAVEN_DISPLAY_FB_COUNT; index++) {
        if (s_frame_history[index].framebuffer == NULL) {
            s_frame_history[index].framebuffer = framebuffer;
            return &s_frame_history[index];
        }
    }
    return NULL;
}

esp_err_t gauge_renderer_init(const uint8_t *background_rgb565, size_t background_size)
{
    if (background_rgb565 == NULL || background_size != GAUGE_RENDERER_BACKGROUND_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    s_gauge_state = (gauge_state_t) {
        .target_value = 0.0f,
        .displayed_value = 0.0f,
        .min_value = GAUGE_RENDERER_MIN_RPM,
        .max_value = GAUGE_RENDERER_MAX_RPM,
        .start_angle_deg = GAUGE_RENDERER_START_ANGLE_DEG,
        .end_angle_deg = GAUGE_RENDERER_END_ANGLE_DEG,
    };
    for (size_t index = 0; index < NEWHAVEN_DISPLAY_FB_COUNT; index++) {
        s_frame_history[index] = (gauge_frame_history_t) {};
    }
    s_background_rgb565 = background_rgb565;
    s_initialized = true;
    return ESP_OK;
}

void gauge_renderer_set_value(float value)
{
    if (!s_initialized) {
        return;
    }
    s_gauge_state.target_value = gauge_clamp_value(value);
}

float gauge_value_to_angle(float value)
{
    const float value_range = s_gauge_state.max_value - s_gauge_state.min_value;
    if (value_range <= 0.0f) {
        return s_gauge_state.start_angle_deg;
    }

    const float clamped_value = gauge_clamp_value(value);
    return s_gauge_state.start_angle_deg +
           ((clamped_value - s_gauge_state.min_value) / value_range) *
               (s_gauge_state.end_angle_deg - s_gauge_state.start_angle_deg);
}

const gauge_state_t *gauge_renderer_get_state(void)
{
    return &s_gauge_state;
}

void gauge_draw_needle(void *framebuffer,
                       int center_x,
                       int center_y,
                       float angle_deg,
                       int length,
                       int base_width,
                       int tip_width,
                       uint32_t rgb)
{
    if (framebuffer == NULL || length <= 0 || base_width <= 0 || tip_width <= 0 ||
        tip_width > base_width) {
        return;
    }

    gauge_draw_needle_shape(framebuffer,
                            center_x,
                            center_y,
                            angle_deg,
                            length,
                            base_width,
                            tip_width,
                            gauge_write_solid_pixel,
                            &rgb);
}

esp_err_t gauge_renderer_render(void *framebuffer)
{
    if (!s_initialized || framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    gauge_frame_history_t *frame_history = gauge_get_frame_history(framebuffer);
    if (frame_history == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (frame_history->has_needle) {
        gauge_draw_needle_shape(framebuffer,
                                GAUGE_RENDERER_CENTER_X,
                                GAUGE_RENDERER_CENTER_Y,
                                frame_history->angle_deg,
                                GAUGE_RENDERER_NEEDLE_LENGTH,
                                GAUGE_RENDERER_NEEDLE_BASE_WIDTH,
                                GAUGE_RENDERER_NEEDLE_TIP_WIDTH,
                                gauge_write_background_pixel,
                                s_background_rgb565);
    }

    s_gauge_state.displayed_value += GAUGE_RENDERER_SMOOTHING_COEFFICIENT *
                                     (s_gauge_state.target_value - s_gauge_state.displayed_value);
    frame_history->angle_deg = gauge_value_to_angle(s_gauge_state.displayed_value);
    gauge_draw_needle(framebuffer,
                      GAUGE_RENDERER_CENTER_X,
                      GAUGE_RENDERER_CENTER_Y,
                      frame_history->angle_deg,
                      GAUGE_RENDERER_NEEDLE_LENGTH,
                      GAUGE_RENDERER_NEEDLE_BASE_WIDTH,
                      GAUGE_RENDERER_NEEDLE_TIP_WIDTH,
                      GAUGE_RENDERER_NEEDLE_COLOR_WHITE);
    frame_history->has_needle = true;
    return ESP_OK;
}
