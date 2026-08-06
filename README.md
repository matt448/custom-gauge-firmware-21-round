# Newhaven NHD-2.1-480480AF-ASXP ESP32-P4 bring-up

Minimal native ESP-IDF bring-up project for a Newhaven NHD-2.1-480480AF-ASXP
round LCD connected to a Waveshare ESP32-P4-WIFI6 board through the ESP32-P4
MIPI DSI PHY. It does not use Arduino, LVGL, SPI, I2C, RGB-parallel pins, or
touch support.

## ESP-IDF requirement

This project was written and checked against **ESP-IDF v6.0.2**. The connected
ESP32-P4 is revision v1.3, so `sdkconfig.defaults` selects the mutually
exclusive pre-v3 SoC family with `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and a
minimum revision of v1.0. Pre-v3 silicon requires the 360 MHz CPU setting,
which is also pinned in the defaults. Its legacy DSI PHY requires the
`PLL_F20M` reference clock; the application selects ESP-IDF's
`MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY` rather than the v3+ XTAL default.
It uses the native `esp_lcd` MIPI DSI APIs:

- `esp_lcd_new_dsi_bus()`
- `esp_lcd_new_panel_io_dbi()`
- `esp_lcd_new_panel_dpi()`
- `esp_lcd_dpi_panel_get_frame_buffer()`

The reference installed example is
`examples/peripherals/lcd/mipi_dsi/main/mipi_dsi_lcd_example_main.c`.

## Build, flash, and monitor

Open an ESP-IDF shell with ESP-IDF v6.0.2 active, then run:

```text
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port selected for the Waveshare ESP32-P4-WIFI6
board. Exit the monitor with `Ctrl+]`.

## Required hardware wiring

| LCD FFC pin | Function | Connection |
| --- | --- | --- |
| 3 | VDD | Regulated display logic supply; **do not exceed 3.3 V** |
| 4, 7, 10 | GND | Common ground |
| 5 | DN0 | ESP32-P4 MIPI DSI data lane 0 negative |
| 6 | DP0 | ESP32-P4 MIPI DSI data lane 0 positive |
| 8 | CN | ESP32-P4 MIPI DSI clock negative |
| 9 | CP | ESP32-P4 MIPI DSI clock positive |
| 33 | RESETX | A user-selected ESP32-P4 GPIO; active low |
| 38 | IM0 | Tie high |
| 39 | IM1 | Tie low |
| 40 | IM2 | Tie high |

IM0=1, IM1=0, IM2=1 selects native MIPI DSI. DSI lane 1 is deliberately not
configured or used.

Pins 1 (LED_K) and 2 (LED_A) require an **external constant-current backlight
driver** suitable for approximately 100 mA and approximately 6.0 V nominal.
Do not connect either backlight pin directly to an ESP32-P4 GPIO.

## Configuration

All panel settings are in [`main/display_config.h`](main/display_config.h).
RESETX on FFC pin 33 is configured for ESP32-P4 GPIO2, and
`NEWHAVEN_DISPLAY_RESET_GPIO_CONFIGURED` is enabled.

The reset sequence drives RESETX high, drives it low for 1 ms, releases it
high, then waits 120 ms before any DSI command transaction. It assumes the
specified external 10 kOhm pull-up to display logic VDD.

Compile-time settings include:

- `NEWHAVEN_DISPLAY_COLOR_ORDER`: RGB or BGR test-pixel byte order
- `NEWHAVEN_DISPLAY_ROTATION`: 0, 90, 180, or 270 degrees
- `NEWHAVEN_DISPLAY_RESET_GPIO`
- `NEWHAVEN_DISPLAY_FB_COUNT`: one or two PSRAM-backed framebuffers
- `NEWHAVEN_DISPLAY_PIXEL_FORMAT`: RGB888 (default) or RGB565

ESP-IDF v6.0.2 supports RGB888 and RGB565 for the ESP32-P4 MIPI DSI DPI
driver. RGB888 is the default. The driver allocates display framebuffers in
DMA-capable PSRAM; `sdkconfig.defaults` enables the board's PSRAM support.

Rotation is applied while drawing the diagnostic framebuffer. Do not send an
unverified ST7701S orientation/MADCTL command solely to rotate the panel.

The DSI PHY is powered before DSI bus creation through ESP32-P4 internal LDO
channel 3 at 2.5 V. This follows the installed ESP-IDF MIPI DSI LCD example
and Espressif's ST7701 MIPI test. It powers only `VDD_MIPI_DPHY`; it does not
power the LCD's VDD or the external backlight driver.

## Video timing and lane configuration

| Setting | Value |
| --- | ---: |
| DSI data lanes | 1 (lane 0 only) |
| Pixel clock | 18 MHz |
| Active resolution | 480 x 480 |
| Horizontal front porch | 40 |
| Horizontal back porch | 60 |
| Horizontal sync pulse | 20 |
| Vertical front porch | 10 |
| Vertical back porch | 10 |
| Vertical sync pulse | 6 |

The configured lane rate is 500 Mbps. RGB888 at 18 MHz requires 432 Mbps of
active-pixel data on one lane; 500 Mbps provides margin. Confirm or adjust
this value if verified panel initialization data specifies a required DSI lane
rate.

## ST7701S initialization status

This project depends on `espressif/esp_lcd_st7701` v2.0.2~2 from Espressif's
`esp-iot-solution` repository, commit
`b9b4cc9fa85345b46af366a002785e7c6e8ef45d`. It is an ESP32-P4-capable MIPI
DSI driver and accepts a custom `st7701_lcd_init_cmd_t` vendor command array.
Its MIPI initialization routine transmits each entry over the native DSI
command channel with `esp_lcd_panel_io_tx_param()`; no external SPI wiring is
used.

[`newhaven_display.c`](main/newhaven_display.c) contains the exact ST7701S
`display_Init()` register sequence published by Newhaven for this panel:

<https://support.newhavendisplay.com/hc/en-us/articles/30069060235927-NHD-2-1-480480AF-ASXP-with-SSD1963-Controller-with-Arduino>

The reference sends register commands through 3-wire SPI before sending pixels
through an SSD1963 RGB controller. This project retains native ESP32-P4 MIPI
DSI video output and sends only those same ST7701S register commands through
the DSI DBI command channel. It does not use the reference's SSD1963, Arduino,
GPIO, SD-card, or image-loading code.

The Newhaven sequence contains generic DCS commands (`0x3A`, `0x11`, `0x29`,
and `0x36`), ST7701S command-page/control commands, and its panel-specific
gamma, voltage, gate, and source settings. It preserves the published 120 ms
Sleep Out delay, 10 ms intermediate E8 delay, and 120 ms Display On delay.

## Expected startup logs

With the Newhaven initialization sequence:

```text
I (....) main: Starting Newhaven NHD-2.1-480480AF-ASXP bring-up
I (....) newhaven_display: Configuring and resetting RESETX
I (....) newhaven_display: Creating one-lane MIPI DSI bus on data lane 0
I (....) newhaven_display: Creating MIPI DSI command interface
I (....) newhaven_display: Creating 480x480 MIPI DSI ST7701S panel with 1 framebuffer(s)
I (....) newhaven_display: Initializing ST7701S controller
I (....) newhaven_display: Sending Newhaven NHD-2.1-480480AF-ASXP ST7701S initialization sequence
```

The driver then starts MIPI DSI video output and animates a white needle over
the embedded static background at [`assets/rpm_gauge.rgb565`](assets/rpm_gauge.rgb565).
The asset must be a 480 x 480, big-endian RGB565 image (460,800 bytes).
Generate it from a PNG with:

```powershell
python tools\png_to_rgb565.py path\to\rpm_gauge.png
```

Copy the generated `rpm_gauge.rgb565` to `assets\rpm_gauge.rgb565`, then
rebuild and flash. The application expands the asset into both RGB888
framebuffers at startup. Each 30 FPS frame restores only the prior needle's
pixels from the RGB565 asset, draws the new three-pixel-wide white needle and
center hub in the inactive framebuffer, then presents that completed
framebuffer to DSI scanout.

## Gauge animation

The independent `gauge_renderer` module currently generates a synthetic test
value that sweeps from 0 to 6000 RPM over approximately three seconds and
returns to 0 RPM over the next three seconds. Its displayed value uses an
exponential smoothing coefficient of 0.18.

The needle center is `(240, 240)` and its center-to-tip radius is 200 pixels.
The white needle has an 11-pixel base and tapers to a three-pixel tip. Values map linearly from 0 RPM at
270 degrees (down) to 6000 RPM at 0 degrees (right). Each 1000 RPM major
marker is 45 degrees counterclockwise from the previous marker. The renderer
uses Cartesian angles, so it negates the display-space Y sine term; these
constants can be calibrated later without changing the panel setup.

The default framebuffer count is two. The DSI driver scans one PSRAM buffer
while the renderer updates the other, eliminating visible tearing and avoiding
the memory-bandwidth spikes that can cause a DSI-bridge blue underrun frame.

## Troubleshooting

- **Blank display:** Confirm VDD is within range, the backlight driver is
  enabled independently, the reset GPIO is correct, IM straps are 1/0/1, and
  verify the panel supply rails and lane wiring.
- **White display:** Check RESETX wiring and the table's sleep-out/display-on
  sequence. A lit white panel can indicate that backlight power is present
  while the controller was never initialized.
- **Flickering display:** Verify DSI clock/data lane differential routing,
  the board's DSI PHY power source, PSRAM configuration, lane rate, and
  18 MHz pixel-clock timing.
- **Shifted or clipped image:** Recheck all porch and sync values in
  `display_config.h` and confirm the source PNG is exactly 480 x 480.
- **Swapped red and blue:** Change `NEWHAVEN_DISPLAY_COLOR_ORDER` and, once
  documented, add the corresponding ST7701S controller color-order command
  to the verified initialization table.

# Newhaven example code
GitHub: https://github.com/newhavendisplay
List of Example Code: https://support.newhavendisplay.com/hc/en-us/categories/4409527834135-Example-Code/
2.1" Display example code: https://support.newhavendisplay.com/hc/en-us/articles/30069060235927-NHD-2-1-480480AF-ASXP-with-SSD1963-Controller-with-Arduino