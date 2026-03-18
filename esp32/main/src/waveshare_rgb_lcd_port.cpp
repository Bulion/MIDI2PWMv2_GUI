#include "waveshare_rgb_lcd_port.h"

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <esp_log.h>

namespace waveshare
{
namespace
{

constexpr char TAG[] = "waveshare_lcd";
constexpr int LcdHRes = 800;
constexpr int LcdVRes = 480;
constexpr int PixelClockHz = 21'000'000;
constexpr int BitPerPixel = 16;
constexpr int RgbDataWidth = 16;
constexpr int NumFramebuffers = 2;
constexpr int BounceBufferSize = LcdHRes * 30;
constexpr int GpioVSync = 3;
constexpr int GpioHSync = 46;
constexpr int GpioDE = 5;
constexpr int GpioPClk = 7;
constexpr int GpioData[] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};

constexpr bool TouchEnabled = true;
constexpr int I2cSclIo = 9;
constexpr int I2cSdaIo = 8;
constexpr i2c_port_t I2cPort = I2C_NUM_0;
constexpr int I2cFreqHz = 400'000;
constexpr gpio_num_t TouchRstGpio = GPIO_NUM_NC;
constexpr gpio_num_t TouchIntGpio = GPIO_NUM_NC;
constexpr int I2cTimeoutMs = 1000;

esp_err_t i2c_master_init()
{
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = I2cSdaIo;
    i2c_conf.scl_io_num = I2cSclIo;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2cFreqHz;
    ESP_ERROR_CHECK(i2c_param_config(I2cPort, &i2c_conf));
    return i2c_driver_install(I2cPort, i2c_conf.mode, 0, 0, 0);
}
} // namespace

esp_err_t rgb_lcd_backlight_on()
{
    return ESP_OK;
}

esp_err_t rgb_lcd_backlight_off()
{
    return ESP_OK;
}

LcdInitResult rgb_lcd_init()
{
    LcdInitResult result;

    ESP_LOGI(TAG, "Initializing RGB LCD panel...");

    esp_lcd_panel_handle_t panel_handle = nullptr;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings =
            {.pclk_hz = PixelClockHz,
             .h_res = LcdHRes,
             .v_res = LcdVRes,
             .hsync_pulse_width = 4,
             .hsync_back_porch = 8,
             .hsync_front_porch = 8,
             .vsync_pulse_width = 4,
             .vsync_back_porch = 8,
             .vsync_front_porch = 8,
             .flags = {.pclk_active_neg = 1}},
        .data_width = RgbDataWidth,
        .bits_per_pixel = BitPerPixel,
        .num_fbs = NumFramebuffers,
        .bounce_buffer_size_px = BounceBufferSize,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = GpioHSync,
        .vsync_gpio_num = GpioVSync,
        .de_gpio_num = GpioDE,
        .pclk_gpio_num = GpioPClk,
        .disp_gpio_num = -1,
        .data_gpio_nums =
            {GpioData[0],
             GpioData[1],
             GpioData[2],
             GpioData[3],
             GpioData[4],
             GpioData[5],
             GpioData[6],
             GpioData[7],
             GpioData[8],
             GpioData[9],
             GpioData[10],
             GpioData[11],
             GpioData[12],
             GpioData[13],
             GpioData[14],
             GpioData[15]},
        .flags = {
            .fb_in_psram = 1
        }};

    result.error = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (result.error != ESP_OK) {
        return result;
    }
    result.error = esp_lcd_panel_init(panel_handle);
    if (result.error != ESP_OK) {
        return result;
    }
    result.panel_handle = panel_handle;

    esp_lcd_touch_handle_t touch_handle = nullptr;

    if constexpr (TouchEnabled) {
        ESP_LOGI(TAG, "Initializing I2C for GT911 touch...");
        result.error = i2c_master_init();
        if (result.error != ESP_OK) {
            return result;
        }

        esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
        const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        result.error =
            esp_lcd_new_panel_io_i2c(static_cast<esp_lcd_i2c_bus_handle_t>(I2cPort), &tp_io_cfg, &tp_io_handle);
        if (result.error != ESP_OK) {
            return result;
        }

        ESP_LOGI(TAG, "Initializing GT911 touch controller...");
        const esp_lcd_touch_config_t tp_cfg = {
            .x_max = LcdHRes,
            .y_max = LcdVRes,
            .rst_gpio_num = TouchRstGpio,
            .int_gpio_num = TouchIntGpio,
            .levels = {.reset = 0, .interrupt = 0},
            .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0}};
        result.error = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
        if (result.error != ESP_OK) {
            return result;
        }
        result.touch_handle = touch_handle;
    }

    return result;
}

} // namespace waveshare
