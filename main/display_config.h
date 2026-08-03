#pragma once

#include "driver/gpio.h"

#define NEWHAVEN_DISPLAY_H_RES                 480
#define NEWHAVEN_DISPLAY_V_RES                 480
#define NEWHAVEN_DISPLAY_PIXEL_CLOCK_MHZ       18

#define NEWHAVEN_DISPLAY_H_FRONT_PORCH         40
#define NEWHAVEN_DISPLAY_H_BACK_PORCH          60
#define NEWHAVEN_DISPLAY_H_SYNC_PULSE          20
#define NEWHAVEN_DISPLAY_V_FRONT_PORCH         10
#define NEWHAVEN_DISPLAY_V_BACK_PORCH          10
#define NEWHAVEN_DISPLAY_V_SYNC_PULSE          6

#define NEWHAVEN_DISPLAY_DSI_BUS_ID            0
#define NEWHAVEN_DISPLAY_DSI_DATA_LANES        1
/*
 * 18 MHz * 24 bits/pixel requires at least 432 Mbps on one data lane.
 * 500 Mbps leaves protocol overhead margin; confirm against the panel's
 * verified DSI initialization data if it specifies another lane rate.
 */
#define NEWHAVEN_DISPLAY_DSI_LANE_BIT_RATE_MBPS 500

/*
 * GPIO2 is wired to FFC pin 33 (RESETX). The display has an external
 * 10 kOhm pull-up.
 */
#define NEWHAVEN_DISPLAY_RESET_GPIO            GPIO_NUM_2
#define NEWHAVEN_DISPLAY_RESET_GPIO_CONFIGURED 1

#define NEWHAVEN_DISPLAY_FB_COUNT              1

#define NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888   888
#define NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB565   565
#define NEWHAVEN_DISPLAY_PIXEL_FORMAT          NEWHAVEN_DISPLAY_PIXEL_FORMAT_RGB888

#define NEWHAVEN_DISPLAY_COLOR_ORDER_RGB       0
#define NEWHAVEN_DISPLAY_COLOR_ORDER_BGR       1
#define NEWHAVEN_DISPLAY_COLOR_ORDER           NEWHAVEN_DISPLAY_COLOR_ORDER_RGB

#define NEWHAVEN_DISPLAY_ROTATION_0            0
#define NEWHAVEN_DISPLAY_ROTATION_90           90
#define NEWHAVEN_DISPLAY_ROTATION_180          180
#define NEWHAVEN_DISPLAY_ROTATION_270          270
#define NEWHAVEN_DISPLAY_ROTATION              NEWHAVEN_DISPLAY_ROTATION_0

/*
 * VDD_MIPI_DPHY must be 2.5 V before the DSI PHY can lock. ESP-IDF's native
 * MIPI DSI example and ST7701 MIPI test use internal LDO channel 3 for this
 * rail. This powers the ESP32-P4 PHY only, not panel VDD or the backlight.
 */
#define NEWHAVEN_DISPLAY_ENABLE_DSI_PHY_LDO    1
#define NEWHAVEN_DISPLAY_DSI_PHY_LDO_CHANNEL   3
#define NEWHAVEN_DISPLAY_DSI_PHY_LDO_MV        2500
