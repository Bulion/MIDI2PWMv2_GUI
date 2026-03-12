#include "display_init.h"

#include "waveshare_rgb_lcd_port.h"

#include <cstdlib>
#include <esp_lcd_panel_rgb.h>
#include <slint-esp.h>
#include <span>

namespace gui::esp32
{

void initializeDisplay()
{
    auto lcd = waveshare::rgb_lcd_init();
    if (lcd.error != ESP_OK) {
        abort();
    }

    void *fb0 = nullptr;
    void *fb1 = nullptr;
    esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(lcd.panel_handle, 2, &fb0, &fb1);
    if (err != ESP_OK) {
        abort();
    }

    auto span0 = std::span<slint::platform::Rgb565Pixel>(static_cast<slint::platform::Rgb565Pixel *>(fb0), 800 * 480);
    auto span1 = std::span<slint::platform::Rgb565Pixel>(static_cast<slint::platform::Rgb565Pixel *>(fb1), 800 * 480);

    slint_esp_init(
        {.size = slint::PhysicalSize({800, 480}),
         .panel_handle = lcd.panel_handle,
         .touch_handle = lcd.touch_handle,
         .buffer1 = span0,
         .buffer2 = span1,
         .rotation = slint::platform::SoftwareRenderer::RenderingRotation::NoRotation,
         .byte_swap = false});
}

} // namespace gui::esp32
