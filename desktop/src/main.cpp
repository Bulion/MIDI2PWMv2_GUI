#include <slint.h>

#include "app-window.h"
#include "gui_common/log.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"
#include "serial_backend.h"

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

static uint16_t parseNoteString(const std::string &note_str);

using gui::common::ChannelTelemetryViewModel;
using gui::common::ConnectionViewModel;
using gui::common::MidiMessageViewModel;
using gui::common::ConnectionBackend;

struct DesktopController
{
    slint::ComponentHandle<AppWindow> app;
    ConnectionViewModel &view_model;
    MidiMessageViewModel &midi_view_model;
    std::atomic<bool> is_assigning_note{false};
    std::atomic<int> channel_awaiting_note{-1};
    std::vector<gui::common::PortInfo> cached_ports;

    void OnMidiMessage(const midi2pwm::midi::ChannelMessageT &message)
    {
        bool is_note_message = (message.message_type == midi2pwm::midi::ChannelMessageType::NoteOn ||
                                message.message_type == midi2pwm::midi::ChannelMessageType::NoteOff);

        GUI_LOG_VERBOSE("MIDI", "Received MIDI message: type=%d, channel=%d, data1=%u, data2=%u",
                        static_cast<int>(message.message_type), message.channel, message.data1, message.data2);

        if (is_assigning_note.load()) {
            GUI_LOG_DEBUG("Assignment", "Processing MIDI during assignment mode: is_note=%d, data1=%u",
                          is_note_message, message.data1);

            if (is_note_message && message.data1 <= 127) {
                int channel_idx = channel_awaiting_note.exchange(-1);
                is_assigning_note.store(false);

                if (channel_idx >= 0) {
                    std::string note_name = midiNoteToString(static_cast<uint16_t>(message.data1));
                    slint::SharedString note_str{note_name.c_str()};

                    GUI_LOG_INFO("Assignment", "Assigning note %s (MIDI %u) to channel %d",
                                 note_name.c_str(), message.data1, channel_idx);

                    slint::invoke_from_event_loop([handle = app, note_str]() {
                        GUI_LOG_DEBUG("Assignment", "Invoking note_assigned_from_backend callback on UI thread");
                        handle->invoke_note_assigned_from_backend(note_str);
                    });
                } else {
                    GUI_LOG_WARNING("Assignment", "Assignment completed but channel_idx was invalid: %d", channel_idx);
                }
            } else {
                if (!is_note_message) {
                    GUI_LOG_DEBUG("Assignment", "Ignoring non-note message during assignment: type=%d",
                                  static_cast<int>(message.message_type));
                } else {
                    GUI_LOG_WARNING("Assignment", "Received out-of-range note during assignment: %u", message.data1);
                }
            }
        }

        midi_view_model.updateFromChannelMessage(message);
    }

    void OnMessage(const MidiMessageViewModel::MessageString &text) const
    {
        slint::SharedString shared{text.c_str()};
        slint::invoke_from_event_loop([handle = app, shared]() {
            handle->set_midi_message(shared);
        });
    }

    static std::string midiNoteToString(uint16_t note_number)
    {
        static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int octave = (note_number / 12) - 1;
        int note_idx = note_number % 12;
        return std::string(NOTE_NAMES[note_idx]) + std::to_string(octave);
    }

    void OnChannelTelemetry(const ChannelTelemetryViewModel::ChannelsArray &channels) const
    {
        slint::invoke_from_event_loop([handle = app, channels]() {
            auto model = std::make_shared<slint::VectorModel<ChannelData>>();

            for (const auto &channelData : channels) {
                ChannelData slintChannelData;
                slintChannelData.note = slint::SharedString(channelData.note.c_str());
                slintChannelData.voltage = channelData.voltage;
                slintChannelData.current = channelData.current;
                slintChannelData.min_value = channelData.minValue;
                slintChannelData.max_value = channelData.maxValue;
                slintChannelData.middle_value = channelData.middleValue;
                slintChannelData.fault = slint::SharedString(channelData.fault.c_str());
                model->push_back(slintChannelData);
            }

            handle->set_channel_model(model);
        });
    }

    void OnPortsChanged(const std::vector<gui::common::PortInfo> &ports)
    {
        cached_ports = ports;

        auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (const auto &portInfo : ports) {
            model->push_back(slint::SharedString(portInfo.friendlyName.c_str()));
        }

        const bool empty = ports.empty();
        slint::invoke_from_event_loop([handle = app, model, empty]() {
            handle->set_available_ports(model);
            if (empty) {
                handle->set_selected_port_index(-1);
                handle->set_connection_status("No serial ports found");
            } else {
                handle->set_selected_port_index(0);
                handle->set_connection_status("");
            }
        });
    }

    void OnConnectionChanged(bool connected) const
    {
        GUI_LOG_INFO("Connection", "Connection state changed: %s", connected ? "CONNECTED" : "DISCONNECTED");

        slint::invoke_from_event_loop([this, handle = app, connected]() {
            if (connected) {
                bool fully_connected = view_model.isFullyConnected();
                GUI_LOG_DEBUG("Connection", "Connection established, fully_connected=%d", fully_connected);

                handle->set_show_connection_screen(false);
                handle->set_waiting_for_device_data(!fully_connected);
                handle->set_connection_status("");

                if (fully_connected) {
                    GUI_LOG_INFO("Connection", "Device fully connected and ready");
                } else {
                    GUI_LOG_INFO("Connection", "Waiting for device telemetry data");
                }
            } else {
                GUI_LOG_DEBUG("Connection", "Showing connection screen, refreshing ports");
                handle->set_show_connection_screen(true);
                handle->set_waiting_for_device_data(false);
                handle->set_connection_status("Connection lost. Select a port to reconnect.");
                view_model.refreshPorts();
            }
        });
    }
};

int main()
{
    gui::common::InitializeLibcommLogging();

    auto app = AppWindow::create();

    app->window().set_size(slint::LogicalSize({800, 520}));

    MidiMessageViewModel message_view_model;
    ChannelTelemetryViewModel channel_telemetry_view_model;
    gui::desktop::SerialBackend backend;
    ConnectionViewModel connection_view_model{backend, message_view_model, channel_telemetry_view_model};

    DesktopController controller{app, connection_view_model, message_view_model};

    message_view_model.setUpdateCallback(
        MidiMessageViewModel::UpdateCallback::create<DesktopController, &DesktopController::OnMessage>(controller));
    channel_telemetry_view_model.setUpdateCallback(
        ChannelTelemetryViewModel::UpdateCallback::create<DesktopController, &DesktopController::OnChannelTelemetry>(controller));
    connection_view_model.setPortsChangedCallback(
        ConnectionViewModel::PortsChangedCallback::create<DesktopController, &DesktopController::OnPortsChanged>(controller));
    connection_view_model.setConnectionChangedCallback(
        ConnectionViewModel::ConnectionChangedCallback::create<DesktopController, &DesktopController::OnConnectionChanged>(controller));
    connection_view_model.setRawMidiMessageCallback(
        ConnectionViewModel::RawMidiMessageCallback::create<DesktopController, &DesktopController::OnMidiMessage>(controller));

    auto initial_ports = connection_view_model.refreshPorts();
    controller.OnPortsChanged(initial_ports);

    app->on_refresh_ports([&connection_view_model]() {
        connection_view_model.refreshPorts();
    });

    app->on_connect_port([&controller, &connection_view_model, app](const slint::SharedString &friendlyName) {
        if (friendlyName.empty()) {
            slint::invoke_from_event_loop([app]() {
                app->set_connection_status("Select a port before connecting");
            });
            return;
        }

        std::string portPathToConnect;
        std::string friendlyNameStr{friendlyName};

        for (const auto &portInfo : controller.cached_ports) {
            if (portInfo.friendlyName == friendlyNameStr) {
                portPathToConnect = portInfo.portPath;
                break;
            }
        }

        if (portPathToConnect.empty()) {
            slint::invoke_from_event_loop([app]() {
                app->set_connection_status("Port not found");
            });
            return;
        }

        GUI_LOG_INFO("Connect", "Connecting to port path: %s (from friendly name: %s)",
                     portPathToConnect.c_str(), friendlyNameStr.c_str());

        if (!connection_view_model.connect(portPathToConnect)) {
            slint::invoke_from_event_loop([app]() {
                app->set_connection_status("Failed to open selected port");
            });
        } else {
            slint::invoke_from_event_loop([app]() {
                app->set_show_connection_screen(false);
                app->set_waiting_for_device_data(true);
                app->set_connection_status("");
            });
        }
    });

    app->on_save_channel_config([&connection_view_model, app](int channel_idx, slint::SharedString note_str,
                                                         float min_val, float middle_val, float max_val) {
        GUI_LOG_INFO("SaveConfig", "Callback invoked for channel %d", channel_idx);

        uint16_t note_number = 255;

        if (!note_str.empty()) {
            std::string note_string{note_str};
            GUI_LOG_DEBUG("SaveConfig", "Parsing note string: %s", note_string.c_str());
            note_number = parseNoteString(note_string);
            GUI_LOG_DEBUG("SaveConfig", "Parsed note number: %u", note_number);
        }

        midi2pwm::pwm::ChannelConfigT config;
        config.channel_number = static_cast<uint16_t>(channel_idx);
        config.configuration = midi2pwm::pwm::ChannelConfiguration::FullBridge;
        config.note = note_number;
        config.min_point = min_val;
        config.midpoint = middle_val;
        config.max_point = max_val;

        GUI_LOG_INFO("SaveConfig", "Sending config: ch=%u, note=%u, min=%f, mid=%f, max=%f",
                     config.channel_number, config.note, config.min_point, config.midpoint, config.max_point);

        bool success = connection_view_model.sendChannelConfig(config);

        GUI_LOG_INFO("SaveConfig", "Send result: %s", success ? "SUCCESS" : "FAILED");

        if (!success) {
            app->set_connection_status("Failed to send configuration");
        } else {
            app->set_connection_status("Configuration saved");
        }

        GUI_LOG_INFO("SaveConfig", "Callback completed");
    });

    app->on_assign_note_clicked([&controller](int channel_idx) {
        GUI_LOG_INFO("Assignment", "Entering note assignment mode for channel %d", channel_idx);
        controller.is_assigning_note.store(true);
        controller.channel_awaiting_note.store(channel_idx);
    });

    app->on_note_assigned_from_backend([app](slint::SharedString note_str) {
        GUI_LOG_INFO("Assignment", "note_assigned_from_backend callback received: note=%s",
                     std::string(note_str).c_str());

        app->set_temp_note(note_str);
        app->set_temp_is_assigning(false);
        app->set_temp_popup_dirty(true);

        GUI_LOG_DEBUG("Assignment", "Updated temp_note to %s, cleared assignment flag, and marked popup dirty",
                      std::string(note_str).c_str());
    });

    app->on_popup_closed([&controller]() {
        if (controller.is_assigning_note.load()) {
            GUI_LOG_INFO("Assignment", "Popup closed during assignment - canceling assignment mode");
            controller.is_assigning_note.store(false);
            controller.channel_awaiting_note.store(-1);
        }
    });

    app->on_reset_note_clicked([](int channel_idx) {
    });

    app->set_midi_message("No midi message received yet");

    slint::Timer timer(std::chrono::milliseconds(100), [&connection_view_model]() {
        connection_view_model.update();
    });

    app->run();

    GUI_LOG_INFO("Shutdown", "Application window closed, disconnecting from device");
    connection_view_model.disconnect();

    GUI_LOG_INFO("Shutdown", "Cleanup complete, exiting");
    return 0;
}

static uint16_t parseNoteString(const std::string &note_str)
{
    if (note_str.empty() || note_str == "---") {
        return 255;
    }

    static const std::unordered_map<std::string, int> NOTE_MAP = {
        {"C", 0}, {"C#", 1}, {"D", 2}, {"D#", 3}, {"E", 4}, {"F", 5},
        {"F#", 6}, {"G", 7}, {"G#", 8}, {"A", 9}, {"A#", 10}, {"B", 11}
    };

    size_t octave_pos = note_str.find_first_of("0123456789-");
    if (octave_pos == std::string::npos || octave_pos == 0) {
        return 255;
    }

    std::string note_name = note_str.substr(0, octave_pos);
    std::string octave_str = note_str.substr(octave_pos);

    if (octave_str.empty()) {
        return 255;
    }

    int octave = 0;
    try {
        octave = std::stoi(octave_str);
    } catch (const std::exception&) {
        return 255;
    }

    auto it = NOTE_MAP.find(note_name);
    if (it == NOTE_MAP.end()) {
        return 255;
    }

    int midi_note = (octave + 1) * 12 + it->second;
    if (midi_note < 0 || midi_note > 127) {
        return 255;
    }

    return static_cast<uint16_t>(midi_note);
}
