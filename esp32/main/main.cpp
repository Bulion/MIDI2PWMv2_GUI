#include "app-window.h"
#include "uart_backend.h"
#include "waveshare_rgb_lcd_port.h"

#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"

#include <slint-esp.h>
#include <slint.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>

using gui::common::ConnectionViewModel;
using gui::common::MidiMessageViewModel;
using gui::common::ChannelTelemetryViewModel;
using gui::common::ConnectionBackend;

namespace
{

void initialize_display()
{
    auto lcd = waveshare::rgb_lcd_init();
    if (lcd.error != ESP_OK) {
        printf("LCD init failed: %d\n", lcd.error);
        abort();
    }

    void *fb0 = nullptr;
    void *fb1 = nullptr;
    esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(lcd.panel_handle, 2, &fb0, &fb1);
    if (err != ESP_OK) {
        printf("Failed to get framebuffers: %d\n", err);
        abort();
    }

    auto span0 = std::span<slint::platform::Rgb565Pixel>(
        static_cast<slint::platform::Rgb565Pixel *>(fb0), 800 * 480);
    auto span1 = std::span<slint::platform::Rgb565Pixel>(
        static_cast<slint::platform::Rgb565Pixel *>(fb1), 800 * 480);

    slint_esp_init({.size = slint::PhysicalSize({800, 480}),
                    .panel_handle = lcd.panel_handle,
                    .touch_handle = lcd.touch_handle,
                    .buffer1 = span0,
                    .buffer2 = span1,
                    .rotation = slint::platform::SoftwareRenderer::RenderingRotation::NoRotation,
                    .byte_swap = false});
}

} // namespace

struct Esp32Controller
{
    slint::ComponentHandle<AppWindow> app;
    ConnectionViewModel &view_model;

    void OnMessage(const MidiMessageViewModel::MessageString &text) const
    {
        slint::SharedString shared{text.c_str()};
        slint::invoke_from_event_loop([handle = app, shared]() {
            handle->set_midi_message(shared);
        });
    }

    void OnPortsChanged(const std::vector<std::string> &ports) const
    {
        auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (const auto &port : ports) {
            model->push_back(slint::SharedString(port.c_str()));
        }

        const bool empty = ports.empty();
        slint::invoke_from_event_loop([handle = app, model, empty]() {
            handle->set_available_ports(model);
            handle->set_selected_port_index(empty ? -1 : 0);
            handle->set_connection_status(empty ? "No UART available" : "");
        });
    }

    void OnConnectionChanged(bool connected) const
    {
        slint::invoke_from_event_loop([this, handle = app, connected]() {
            if (connected) {
                bool fully_connected = view_model.isFullyConnected();

                handle->set_show_connection_screen(false);
                handle->set_waiting_for_device_data(!fully_connected);
                handle->set_connection_status("");

                if (fully_connected) {
                    printf("Device fully connected and ready\n");
                } else {
                    printf("Waiting for device telemetry data\n");
                }
            } else {
                handle->set_show_connection_screen(true);
                handle->set_waiting_for_device_data(false);
                handle->set_connection_status("Connection lost");
                view_model.refreshPorts();
            }
        });
    }
};

extern "C" void app_main(void)
{
    initialize_display();

    gui::common::InitializeLibcommLogging();

    auto app = AppWindow::create();

    MidiMessageViewModel message_view_model;
    ChannelTelemetryViewModel channel_telemetry_view_model;
    gui::esp32::UartBackend backend;
    ConnectionViewModel connection_view_model{backend, message_view_model, channel_telemetry_view_model};

    Esp32Controller controller{app, connection_view_model};

    message_view_model.setUpdateCallback(
        MidiMessageViewModel::UpdateCallback::create<Esp32Controller, &Esp32Controller::OnMessage>(controller));
    connection_view_model.setPortsChangedCallback(
        ConnectionViewModel::PortsChangedCallback::create<Esp32Controller, &Esp32Controller::OnPortsChanged>(controller));
    connection_view_model.setConnectionChangedCallback(
        ConnectionViewModel::ConnectionChangedCallback::create<Esp32Controller, &Esp32Controller::OnConnectionChanged>(controller));

    controller.OnPortsChanged(connection_view_model.refreshPorts());

    app->on_refresh_ports([&connection_view_model]() {
        connection_view_model.refreshPorts();
    });

    app->on_connect_port([&connection_view_model, app](const slint::SharedString &port) {
        if (port.is_empty()) {
            slint::invoke_from_event_loop([app]() {
                app->set_connection_status("Select a port before connecting");
            });
            return;
        }

        const std::string port_name{port};

        if (!connection_view_model.connect(port_name)) {
            slint::invoke_from_event_loop([app]() {
                app->set_connection_status("Failed to open UART");
            });
        } else {
            slint::invoke_from_event_loop([app]() {
                app->set_show_connection_screen(false);
                app->set_waiting_for_device_data(true);
                app->set_connection_status("");
            });
        }
    });

    app->set_show_connection_screen(true);
    app->set_midi_message("No midi message received yet");

    slint::Timer timer(std::chrono::milliseconds(100), [&connection_view_model]() {
        connection_view_model.update();
    });

    app->run();
}
