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
    std::atomic<bool> is_assigning_cc{false};
    std::vector<gui::common::PortInfo> cached_ports;

    void OnMidiMessage(const midi2pwm::midi::ChannelMessageT &message)
    {
        bool is_note_message = (message.message_type == midi2pwm::midi::ChannelMessageType::NoteOn ||
                                message.message_type == midi2pwm::midi::ChannelMessageType::NoteOff);

        GUI_LOG_VERBOSE("MIDI", "Received MIDI message: type=%d, channel=%d, data1=%u, data2=%u",
                        static_cast<int>(message.message_type), message.channel, message.data1, message.data2);

        if (is_assigning_note.load()) {
            GUI_LOG_DEBUG("Assignment", "Processing MIDI during note assignment mode: is_note=%d, data1=%u",
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
        } else if (is_assigning_cc.load()) {
            bool is_cc_message = (message.message_type == midi2pwm::midi::ChannelMessageType::ControlChange);

            GUI_LOG_DEBUG("Assignment", "Processing MIDI during CC assignment mode: is_cc=%d, cc_num=%u",
                          is_cc_message, message.data1);

            if (is_cc_message && message.data1 <= 127) {
                is_assigning_cc.store(false);
                int cc_number = static_cast<int>(message.data1);

                GUI_LOG_INFO("Assignment", "Assigning CC number %d", cc_number);

                slint::invoke_from_event_loop([handle = app, cc_number]() {
                    GUI_LOG_DEBUG("Assignment", "Invoking cc_assigned_from_backend callback on UI thread");
                    handle->invoke_cc_assigned_from_backend(cc_number);
                });
            } else {
                if (!is_cc_message) {
                    GUI_LOG_DEBUG("Assignment", "Ignoring non-CC message during CC assignment: type=%d",
                                  static_cast<int>(message.message_type));
                } else {
                    GUI_LOG_WARNING("Assignment", "Received out-of-range CC during assignment: %u", message.data1);
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
                slintChannelData.fault = slint::SharedString(channelData.fault.c_str());
                slintChannelData.mode_type = channelData.mode_type;
                slintChannelData.instant_data.on_level = channelData.instant_data.on_level;
                slintChannelData.instant_data.velocity_sensitive = channelData.instant_data.velocity_sensitive;
                slintChannelData.ramped_data.on_level = channelData.ramped_data.on_level;
                slintChannelData.ramped_data.velocity_sensitive = channelData.ramped_data.velocity_sensitive;
                slintChannelData.ramped_data.attack_time_ms = channelData.ramped_data.attack_time_ms;
                slintChannelData.ramped_data.release_time_ms = channelData.ramped_data.release_time_ms;
                slintChannelData.pulse_data.on_level = channelData.pulse_data.on_level;
                slintChannelData.pulse_data.velocity_sensitive = channelData.pulse_data.velocity_sensitive;
                slintChannelData.pulse_data.attack_time_ms = channelData.pulse_data.attack_time_ms;
                slintChannelData.pulse_data.hold_time_ms = channelData.pulse_data.hold_time_ms;
                slintChannelData.pulse_data.release_time_ms = channelData.pulse_data.release_time_ms;
                slintChannelData.toggle_data.on_level = channelData.toggle_data.on_level;
                slintChannelData.toggle_data.velocity_sensitive = channelData.toggle_data.velocity_sensitive;
                slintChannelData.toggle_data.debounce_delay_ms = channelData.toggle_data.debounce_delay_ms;
                slintChannelData.adsr_data.attack_level = channelData.adsr_data.attack_level;
                slintChannelData.adsr_data.sustain_level = channelData.adsr_data.sustain_level;
                slintChannelData.adsr_data.velocity_sensitive = channelData.adsr_data.velocity_sensitive;
                slintChannelData.adsr_data.attack_time_ms = channelData.adsr_data.attack_time_ms;
                slintChannelData.adsr_data.decay_time_ms = channelData.adsr_data.decay_time_ms;
                slintChannelData.adsr_data.release_time_ms = channelData.adsr_data.release_time_ms;
                slintChannelData.cc_data.cc_number = channelData.cc_data.cc_number;
                slintChannelData.cc_data.center_value = channelData.cc_data.center_value;
                slintChannelData.cc_data.left_max_pwm = channelData.cc_data.left_max_pwm;
                slintChannelData.cc_data.right_max_pwm = channelData.cc_data.right_max_pwm;
                slintChannelData.cc_data.deadband_range = channelData.cc_data.deadband_range;
                slintChannelData.pitchbend_data.base_level = channelData.pitchbend_data.base_level;
                slintChannelData.pitchbend_data.bend_range = channelData.pitchbend_data.bend_range;
                slintChannelData.pitchbend_data.unipolar = channelData.pitchbend_data.unipolar;
                slintChannelData.pitchbend_data.velocity_sensitive = channelData.pitchbend_data.velocity_sensitive;
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

    app->on_save_channel_config([&connection_view_model, app](int channel_idx, slint::SharedString note_str, ModeConfig mode_config) {
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
        config.min_point = 0.0F;
        config.midpoint = 0.0F;
        config.max_point = 0.0F;

        config.output_mode = static_cast<midi2pwm::pwm::OutputModeType>(mode_config.mode_type);

        switch (mode_config.mode_type) {
            case 0: {
                auto instant_params = std::make_unique<midi2pwm::pwm::InstantModeParamsT>();
                instant_params->on_level = static_cast<uint8_t>(mode_config.instant_data.on_level);
                instant_params->velocity_sensitive = mode_config.instant_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::InstantModeParams;
                config.mode_params.value = instant_params.release();
                break;
            }
            case 1: {
                auto ramped_params = std::make_unique<midi2pwm::pwm::RampedModeParamsT>();
                ramped_params->on_level = static_cast<uint8_t>(mode_config.ramped_data.on_level);
                ramped_params->velocity_sensitive = mode_config.ramped_data.velocity_sensitive;
                ramped_params->attack_time_ms = static_cast<uint16_t>(mode_config.ramped_data.attack_time_ms);
                ramped_params->release_time_ms = static_cast<uint16_t>(mode_config.ramped_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::RampedModeParams;
                config.mode_params.value = ramped_params.release();
                break;
            }
            case 2: {
                auto pulse_params = std::make_unique<midi2pwm::pwm::PulseModeParamsT>();
                pulse_params->on_level = static_cast<uint8_t>(mode_config.pulse_data.on_level);
                pulse_params->velocity_sensitive = mode_config.pulse_data.velocity_sensitive;
                pulse_params->attack_time_ms = static_cast<uint16_t>(mode_config.pulse_data.attack_time_ms);
                pulse_params->hold_time_ms = static_cast<uint16_t>(mode_config.pulse_data.hold_time_ms);
                pulse_params->release_time_ms = static_cast<uint16_t>(mode_config.pulse_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PulseModeParams;
                config.mode_params.value = pulse_params.release();
                break;
            }
            case 3: {
                auto toggle_params = std::make_unique<midi2pwm::pwm::ToggleModeParamsT>();
                toggle_params->on_level = static_cast<uint8_t>(mode_config.toggle_data.on_level);
                toggle_params->velocity_sensitive = mode_config.toggle_data.velocity_sensitive;
                toggle_params->debounce_delay_ms = static_cast<uint16_t>(mode_config.toggle_data.debounce_delay_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ToggleModeParams;
                config.mode_params.value = toggle_params.release();
                break;
            }
            case 4: {
                auto adsr_params = std::make_unique<midi2pwm::pwm::ADSRModeParamsT>();
                adsr_params->attack_level = static_cast<uint8_t>(mode_config.adsr_data.attack_level);
                adsr_params->sustain_level = static_cast<uint8_t>(mode_config.adsr_data.sustain_level);
                adsr_params->velocity_sensitive = mode_config.adsr_data.velocity_sensitive;
                adsr_params->attack_time_ms = static_cast<uint16_t>(mode_config.adsr_data.attack_time_ms);
                adsr_params->decay_time_ms = static_cast<uint16_t>(mode_config.adsr_data.decay_time_ms);
                adsr_params->release_time_ms = static_cast<uint16_t>(mode_config.adsr_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ADSRModeParams;
                config.mode_params.value = adsr_params.release();
                break;
            }
            case 5: {
                auto cc_params = std::make_unique<midi2pwm::pwm::CCControlModeParamsT>();
                cc_params->cc_number = static_cast<uint8_t>(mode_config.cc_data.cc_number);
                cc_params->center_value = static_cast<uint8_t>(mode_config.cc_data.center_value);
                cc_params->left_max_pwm = static_cast<uint8_t>(mode_config.cc_data.left_max_pwm);
                cc_params->right_max_pwm = static_cast<uint8_t>(mode_config.cc_data.right_max_pwm);
                cc_params->deadband_range = static_cast<uint8_t>(mode_config.cc_data.deadband_range);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::CCControlModeParams;
                config.mode_params.value = cc_params.release();
                break;
            }
            case 6: {
                auto pitchbend_params = std::make_unique<midi2pwm::pwm::PitchBendModeParamsT>();
                pitchbend_params->base_level = static_cast<uint8_t>(mode_config.pitchbend_data.base_level);
                pitchbend_params->bend_range = static_cast<uint8_t>(mode_config.pitchbend_data.bend_range);
                pitchbend_params->unipolar = mode_config.pitchbend_data.unipolar;
                pitchbend_params->velocity_sensitive = mode_config.pitchbend_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PitchBendModeParams;
                config.mode_params.value = pitchbend_params.release();
                break;
            }
            default:
                GUI_LOG_WARNING("SaveConfig", "Unsupported mode type: %d", mode_config.mode_type);
                break;
        }

        GUI_LOG_INFO("SaveConfig", "Sending config: ch=%u, note=%u, mode=%d",
                     config.channel_number, config.note, mode_config.mode_type);

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

    app->on_assign_cc_clicked([&controller]() {
        GUI_LOG_INFO("Assignment", "Entering CC assignment mode");
        controller.is_assigning_cc.store(true);
    });

    app->on_cc_assigned_from_backend([app](int cc_number) {
        GUI_LOG_INFO("Assignment", "cc_assigned_from_backend callback received: CC=%d", cc_number);

        auto temp_config = app->get_temp_mode_config();
        temp_config.cc_data.cc_number = cc_number;
        app->set_temp_mode_config(temp_config);

        app->set_temp_is_assigning_cc(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_reset_cc_clicked([app]() {
        GUI_LOG_INFO("Assignment", "Resetting CC assignment");

        auto temp_config = app->get_temp_mode_config();
        temp_config.cc_data.cc_number = 0;
        app->set_temp_mode_config(temp_config);

        app->set_temp_popup_dirty(true);
    });

    app->on_popup_closed([&controller, app]() {
        if (controller.is_assigning_note.load()) {
            GUI_LOG_INFO("Assignment", "Popup closed during note assignment - canceling");
            controller.is_assigning_note.store(false);
            controller.channel_awaiting_note.store(-1);
        }
        if (controller.is_assigning_cc.load()) {
            GUI_LOG_INFO("Assignment", "Popup closed during CC assignment - canceling");
            controller.is_assigning_cc.store(false);
            app->set_temp_is_assigning_cc(false);
        }
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
