#include <slint.h>

#include "app-window.h"
#include "gui_common/log.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"
#include "serial_backend.h"

#include "ota_messages_generated.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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
    std::shared_ptr<slint::VectorModel<ChannelData>> channel_model;
    slint::SharedString last_midi_message;

    void OnOtaProgress(const midi2pwm::ota::OtaProgress &progress)
    {
        auto status = progress.status();
        auto chunks_received = progress.chunks_received();
        auto total_chunks = progress.total_chunks();
        std::string error_str = (progress.error_message()) ? progress.error_message()->str() : "";

        slint::invoke_from_event_loop([this, status, chunks_received, total_chunks, error_str]() {
            using Status = midi2pwm::ota::OtaStatus;
            switch (status) {
            case Status::Receiving: {
                float pct = total_chunks > 0
                    ? (static_cast<float>(chunks_received) / static_cast<float>(total_chunks)) * 100.0f
                    : 0.0f;
                app->set_ota_progress_percent(pct);
                app->set_ota_status_text(slint::SharedString("Uploading..."));
                break;
            }
            case Status::Applying:
                app->set_ota_progress_percent(100.0f);
                app->set_ota_status_text(slint::SharedString("Applying firmware..."));
                break;
            case Status::Rebooting:
                app->set_ota_progress_percent(100.0f);
                app->set_ota_status_text(slint::SharedString("Rebooting device..."));
                app->set_ota_is_uploading(false);
                break;
            case Status::Idle:
                app->set_ota_status_text(slint::SharedString("Upload complete"));
                app->set_ota_is_uploading(false);
                break;
            case Status::Error: {
                std::string msg = "Error: " + error_str;
                app->set_ota_status_text(slint::SharedString(msg.c_str()));
                app->set_ota_is_uploading(false);
                break;
            }
            default:
                break;
            }
        });
    }

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

    void OnMessage(const MidiMessageViewModel::MessageString &text)
    {
        slint::SharedString shared{text.c_str()};
        if (shared == last_midi_message) {
            return;
        }
        last_midi_message = shared;
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

    static ChannelData toSlintChannelData(const gui::common::ChannelData &channelData)
    {
        ChannelData slintChannelData;
        slintChannelData.note = slint::SharedString(channelData.note.c_str());
        slintChannelData.voltage = channelData.voltage;
        slintChannelData.current = channelData.currentMa;
        slintChannelData.duty_cycle = channelData.dutyCyclePercent;
        slintChannelData.is_active = channelData.isActive;
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
        return slintChannelData;
    }

    void OnChannelTelemetry(const ChannelTelemetryViewModel::ChannelsArray &channels)
    {
        slint::invoke_from_event_loop([this, handle = app, channels]() {
            if (!channel_model) {
                channel_model = std::make_shared<slint::VectorModel<ChannelData>>();
                for (const auto &channelData : channels) {
                    channel_model->push_back(toSlintChannelData(channelData));
                }
                handle->set_channel_model(channel_model);
                return;
            }

            for (std::size_t i = 0; i < channels.size(); ++i) {
                channel_model->set_row_data(i, toSlintChannelData(channels[i]));
            }
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

    void OnConnectionChanged(bool peerAlive) const
    {
        GUI_LOG_INFO("Connection", "Connection state changed: %s", peerAlive ? "PEER_ALIVE" : "PEER_LOST");

        slint::invoke_from_event_loop([this, handle = app, peerAlive]() {
            if (peerAlive) {
                handle->set_show_connection_screen(false);
                handle->set_waiting_for_device_data(false);
                handle->set_reconnecting_overlay_visible(false);
                handle->set_connection_status("");
            } else {
                if (view_model.isConnected()) {
                    handle->set_reconnecting_overlay_visible(true);
                } else {
                    handle->set_show_connection_screen(true);
                    handle->set_waiting_for_device_data(false);
                    handle->set_reconnecting_overlay_visible(false);
                    handle->set_connection_status("Connection lost. Select a port to reconnect.");
                    view_model.refreshPorts();
                }
            }
        });
    }
};

namespace ota_upload
{

static constexpr std::size_t CHUNK_SIZE = 3840;

static std::uint32_t computeCrc32(const std::vector<std::uint8_t> &data)
{
    std::uint32_t crc = 0xFFFFFFFF;
    for (std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320) : (crc >> 1);
        }
    }
    return ~crc;
}

static std::vector<std::uint8_t> readFile(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static std::string openFileDialog()
{
    FILE *fp = popen("zenity --file-selection --title='Select firmware binary' --file-filter='Binary files | *.bin' 2>/dev/null", "r");
    if (!fp) {
        fp = popen("kdialog --getopenfilename . '*.bin' 2>/dev/null", "r");
    }
    if (!fp) {
        return {};
    }
    char buf[1024] = {};
    if (fgets(buf, sizeof(buf), fp) != nullptr) {
        std::string result(buf);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
        pclose(fp);
        return result;
    }
    pclose(fp);
    return {};
}

} // namespace ota_upload

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

    std::atomic<bool> ota_cancel_requested{false};

    connection_view_model.messageProcessor().setOtaProgressCallback(
        gui::common::MessageProcessor::OtaProgressCallback::create<DesktopController, &DesktopController::OnOtaProgress>(controller));

    app->on_ota_browse_firmware([app]() {
        std::string path = ota_upload::openFileDialog();
        if (!path.empty()) {
            slint::invoke_from_event_loop([app, path]() {
                app->set_ota_firmware_path(slint::SharedString(path.c_str()));
                app->set_ota_status_text(slint::SharedString(""));
                app->set_ota_progress_percent(0.0f);
            });
        }
    });

    app->on_ota_start_upload([app, &connection_view_model, &ota_cancel_requested](int target_index) {
        std::string path{app->get_ota_firmware_path()};
        if (path.empty()) {
            return;
        }

        auto firmware = ota_upload::readFile(path);
        if (firmware.empty()) {
            slint::invoke_from_event_loop([app]() {
                app->set_ota_status_text(slint::SharedString("Error: could not read firmware file"));
            });
            return;
        }

        std::uint32_t firmware_crc = ota_upload::computeCrc32(firmware);
        std::uint32_t firmware_size = static_cast<std::uint32_t>(firmware.size());
        auto total_chunks = static_cast<std::uint16_t>((firmware_size + ota_upload::CHUNK_SIZE - 1) / ota_upload::CHUNK_SIZE);

        auto target = (target_index == 0) ? midi2pwm::ota::Target::Stm32 : midi2pwm::ota::Target::Esp32;

        std::string version = std::filesystem::path(path).filename().string();

        ota_cancel_requested.store(false);

        slint::invoke_from_event_loop([app]() {
            app->set_ota_is_uploading(true);
            app->set_ota_progress_percent(0.0f);
            app->set_ota_status_text(slint::SharedString("Starting upload..."));
        });

        std::thread([app, &connection_view_model, &ota_cancel_requested,
                     firmware = std::move(firmware), firmware_crc, firmware_size,
                     total_chunks, target, version]() mutable {
            auto &mp = connection_view_model.messageProcessor();

            if (!mp.sendOtaBegin(target, firmware_size, firmware_crc, version.c_str(), total_chunks)) {
                slint::invoke_from_event_loop([app]() {
                    app->set_ota_status_text(slint::SharedString("Error: failed to send OtaBegin"));
                    app->set_ota_is_uploading(false);
                });
                return;
            }

            for (std::uint16_t i = 0; i < total_chunks; ++i) {
                if (ota_cancel_requested.load()) {
                    mp.sendOtaAbort(target, "Cancelled by user");
                    slint::invoke_from_event_loop([app]() {
                        app->set_ota_status_text(slint::SharedString("Upload cancelled"));
                        app->set_ota_is_uploading(false);
                    });
                    return;
                }

                std::size_t offset = static_cast<std::size_t>(i) * ota_upload::CHUNK_SIZE;
                std::size_t chunk_len = std::min(ota_upload::CHUNK_SIZE, firmware.size() - offset);

                if (!mp.sendOtaData(target, i, firmware.data() + offset, chunk_len)) {
                    mp.sendOtaAbort(target, "Send failed");
                    slint::invoke_from_event_loop([app]() {
                        app->set_ota_status_text(slint::SharedString("Error: failed to send chunk"));
                        app->set_ota_is_uploading(false);
                    });
                    return;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            mp.sendOtaEnd(target);
        }).detach();
    });

    app->on_ota_cancel_upload([&connection_view_model, &ota_cancel_requested, app]() {
        ota_cancel_requested.store(true);
    });

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
                instant_params->on_level = gui::common::uiPercentToDevice(mode_config.instant_data.on_level);
                instant_params->velocity_sensitive = mode_config.instant_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::InstantModeParams;
                config.mode_params.value = instant_params.release();
                break;
            }
            case 1: {
                auto ramped_params = std::make_unique<midi2pwm::pwm::RampedModeParamsT>();
                ramped_params->on_level = gui::common::uiPercentToDevice(mode_config.ramped_data.on_level);
                ramped_params->velocity_sensitive = mode_config.ramped_data.velocity_sensitive;
                ramped_params->attack_time_ms = static_cast<uint16_t>(mode_config.ramped_data.attack_time_ms);
                ramped_params->release_time_ms = static_cast<uint16_t>(mode_config.ramped_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::RampedModeParams;
                config.mode_params.value = ramped_params.release();
                break;
            }
            case 2: {
                auto pulse_params = std::make_unique<midi2pwm::pwm::PulseModeParamsT>();
                pulse_params->on_level = gui::common::uiPercentToDevice(mode_config.pulse_data.on_level);
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
                toggle_params->on_level = gui::common::uiPercentToDevice(mode_config.toggle_data.on_level);
                toggle_params->velocity_sensitive = mode_config.toggle_data.velocity_sensitive;
                toggle_params->debounce_delay_ms = static_cast<uint16_t>(mode_config.toggle_data.debounce_delay_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ToggleModeParams;
                config.mode_params.value = toggle_params.release();
                break;
            }
            case 4: {
                auto adsr_params = std::make_unique<midi2pwm::pwm::ADSRModeParamsT>();
                adsr_params->attack_level = gui::common::uiPercentToDevice(mode_config.adsr_data.attack_level);
                adsr_params->sustain_level = gui::common::uiPercentToDevice(mode_config.adsr_data.sustain_level);
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
                cc_params->left_max_pwm = gui::common::uiPercentToDevice(mode_config.cc_data.left_max_pwm);
                cc_params->right_max_pwm = gui::common::uiPercentToDevice(mode_config.cc_data.right_max_pwm);
                cc_params->deadband_range = static_cast<uint8_t>(mode_config.cc_data.deadband_range);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::CCControlModeParams;
                config.mode_params.value = cc_params.release();
                break;
            }
            case 6: {
                auto pitchbend_params = std::make_unique<midi2pwm::pwm::PitchBendModeParamsT>();
                pitchbend_params->base_level = gui::common::uiPercentToDevice(mode_config.pitchbend_data.base_level);
                pitchbend_params->bend_range = gui::common::uiPercentToDevice(mode_config.pitchbend_data.bend_range);
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
