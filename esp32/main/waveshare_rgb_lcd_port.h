#pragma once

#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_touch_gt911.h>

namespace waveshare
{
struct LcdInitResult {
    esp_lcd_panel_handle_t panel_handle = nullptr;
    esp_lcd_touch_handle_t touch_handle = nullptr;
    esp_err_t error = ESP_OK;
};

LcdInitResult rgb_lcd_init();

esp_err_t rgb_lcd_backlight_on();
esp_err_t rgb_lcd_backlight_off();

} // namespace waveshare
