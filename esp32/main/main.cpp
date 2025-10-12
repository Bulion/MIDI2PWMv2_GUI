#include "app-window.h"
#include "esp_private/usb_phy.h"
#include "waveshare_rgb_lcd_port.h"

#include <memory>
#include <slint-esp.h>
#include <vector>

// clang-format off
#include "tusb.h"
#include "class/midi/midi_host.h"
// clang-format on

static usb_phy_handle_t phy_hdl;

bool usb_init(void)
{
    usb_phy_config_t phy_conf{};
    phy_conf.controller = USB_PHY_CTRL_OTG;
    phy_conf.target = USB_PHY_TARGET_INT;
    phy_conf.otg_mode = USB_OTG_MODE_HOST;
    phy_conf.otg_speed = USB_PHY_SPEED_UNDEFINED;

    usb_new_phy(&phy_conf, &phy_hdl);

    return true;
}

slint::ComponentWeakHandle<AppWindow> g_ui;

void display_midi_message(const uint8_t *data, size_t len)
{
    std::string msg = "MIDI: ";
    for (size_t i = 0; i < len; ++i) {
        char buf[5];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        msg += buf;
    }
    printf("%s\n", msg.c_str());

    slint::invoke_from_event_loop([msg] {
        auto ui = g_ui.lock();
        if (ui) {
            ui.value()->set_midi_message(msg.c_str());
        }
    });
}

extern "C" void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
    printf(
        "MIDI Interface Index = %u, Address = %u, Number of RX cables = %u, Number of TX cables = %u\r\n",
        idx,
        mount_cb_data->daddr,
        mount_cb_data->rx_cable_count,
        mount_cb_data->tx_cable_count);
}

extern "C" void tuh_midi_umount_cb(uint8_t idx)
{
    printf("MIDI Interface Index = %u is unmounted\r\n", idx);
}

extern "C" void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes)
{
    if (xferred_bytes == 0) {
        return;
    }
    printf("MIDI Rx callback. Idx = %u xferred_bytes = %lu\r\n", idx, xferred_bytes);

    uint8_t packet[4];
    uint8_t cable_num = 0;
    while (tuh_midi_stream_read(idx, &cable_num, packet, sizeof(packet))) {
        display_midi_message(packet, 4);
    }
}

extern "C" void app_main(void)
{
    usb_init();

    auto lcd = waveshare::rgb_lcd_init();
    if (lcd.error != ESP_OK) {
        printf("LCD init failed: %d\n", lcd.error);
        return;
    }
    setenv("SLINT_DEBUG_PERFORMANCE", "1", 1);

    void *fb0 = nullptr;
    void *fb1 = nullptr;
    esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(lcd.panel_handle, 2, &fb0, &fb1);
    if (err != ESP_OK) {
        printf("Failed to get framebuffers: %d\n", err);
        return;
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

    xTaskCreatePinnedToCore(
        [](void *) {
            printf("MIDI HOST TASK STARTED\n");
            tusb_rhport_init_t host_init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
            tusb_init(0, &host_init);

            while (1) {
                tuh_task();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        },
        "midi_host",
        8192,
        nullptr,
        5,
        nullptr,
        1);

    auto ui = AppWindow::create();

    g_ui = ui;
    ui->run();
}
