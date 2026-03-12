#include <slint.h>

#include "app-window.h"
#include "gui_common/comm_loop.h"
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

namespace
{

ChannelData toSlintChannelData(const gui::common::ChannelData &channelData)
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

std::string midiNoteToString(uint16_t noteNumber)
{
    static const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (noteNumber / 12) - 1;
    int noteIdx = noteNumber % 12;
    return std::string(NOTE_NAMES[noteIdx]) + std::to_string(octave);
}

struct OtaProgressState
{
    std::atomic<uint32_t> version{0};
    std::mutex mutex;
    midi2pwm::ota::OtaStatus status{midi2pwm::ota::OtaStatus::Idle};
    uint16_t chunksReceived{0};
    uint16_t totalChunks{0};
    std::string errorMessage;
};

static OtaProgressState s_otaProgress;

static void onOtaProgressReceived(const midi2pwm::ota::OtaProgress &progress)
{
    {
        std::lock_guard<std::mutex> lock(s_otaProgress.mutex);
        s_otaProgress.status = progress.status();
        s_otaProgress.chunksReceived = progress.chunks_received();
        s_otaProgress.totalChunks = progress.total_chunks();
        s_otaProgress.errorMessage = progress.error_message() ? progress.error_message()->str() : "";
    }
    s_otaProgress.version.fetch_add(1, std::memory_order_release);
}

} // namespace

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

    MidiMessageViewModel midiViewModel;
    ChannelTelemetryViewModel channelTelemetryViewModel;
    gui::desktop::SerialBackend backend;
    ConnectionViewModel connectionViewModel{backend, midiViewModel, channelTelemetryViewModel};

    gui::common::CommLoop commLoop(connectionViewModel);

    std::atomic<bool> isAssigningNote{false};
    std::atomic<int> channelAwaitingNote{-1};
    std::atomic<bool> isAssigningCc{false};

    std::atomic<bool> otaCancelRequested{false};

    std::atomic<uint32_t> portsVersion{0};
    std::vector<gui::common::PortInfo> cachedPorts;
    std::mutex portsMutex;

    auto &msgProc = connectionViewModel.messageProcessor();
    msgProc.setOtaProgressCallback(
        gui::common::MessageProcessor::OtaProgressCallback::create<&onOtaProgressReceived>());

    std::thread commThread([&commLoop]() {
        while (!commLoop.shouldStop()) {
            commLoop.runOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

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

    app->on_ota_start_upload([app, &connectionViewModel, &otaCancelRequested](int target_index) {
        std::string path{app->get_ota_firmware_path()};
        if (path.empty()) {
            return;
        }

        auto firmware = ota_upload::readFile(path);
        if (firmware.empty()) {
            app->set_ota_status_text(slint::SharedString("Error: could not read firmware file"));
            return;
        }

        std::uint32_t firmware_crc = ota_upload::computeCrc32(firmware);
        std::uint32_t firmware_size = static_cast<std::uint32_t>(firmware.size());
        auto total_chunks = static_cast<std::uint16_t>((firmware_size + ota_upload::CHUNK_SIZE - 1) / ota_upload::CHUNK_SIZE);

        auto target = (target_index == 0) ? midi2pwm::ota::Target::Stm32 : midi2pwm::ota::Target::Esp32;

        std::string version = std::filesystem::path(path).filename().string();

        otaCancelRequested.store(false);

        app->set_ota_is_uploading(true);
        app->set_ota_progress_percent(0.0f);
        app->set_ota_status_text(slint::SharedString("Starting upload..."));

        std::thread([app, &connectionViewModel, &otaCancelRequested,
                     firmware = std::move(firmware), firmware_crc, firmware_size,
                     total_chunks, target, version]() mutable {
            auto &mp = connectionViewModel.messageProcessor();

            if (!mp.sendOtaBegin(target, firmware_size, firmware_crc, version.c_str(), total_chunks)) {
                slint::invoke_from_event_loop([app]() {
                    app->set_ota_status_text(slint::SharedString("Error: failed to send OtaBegin"));
                    app->set_ota_is_uploading(false);
                });
                return;
            }

            for (std::uint16_t i = 0; i < total_chunks; ++i) {
                if (otaCancelRequested.load()) {
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

    app->on_ota_cancel_upload([&otaCancelRequested]() {
        otaCancelRequested.store(true);
    });

    auto initialPorts = connectionViewModel.refreshPorts();
    {
        std::lock_guard<std::mutex> lock(portsMutex);
        cachedPorts = initialPorts;
    }

    auto portsModel = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (const auto &portInfo : initialPorts) {
        portsModel->push_back(slint::SharedString(portInfo.friendlyName.c_str()));
    }
    app->set_available_ports(portsModel);
    if (initialPorts.empty()) {
        app->set_selected_port_index(-1);
        app->set_connection_status("No serial ports found");
    } else {
        app->set_selected_port_index(0);
        app->set_connection_status("");
    }

    app->on_refresh_ports([&connectionViewModel, &cachedPorts, &portsMutex, app]() {
        auto ports = connectionViewModel.refreshPorts();
        {
            std::lock_guard<std::mutex> lock(portsMutex);
            cachedPorts = ports;
        }

        auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (const auto &portInfo : ports) {
            model->push_back(slint::SharedString(portInfo.friendlyName.c_str()));
        }
        app->set_available_ports(model);
        if (ports.empty()) {
            app->set_selected_port_index(-1);
            app->set_connection_status("No serial ports found");
        } else {
            app->set_selected_port_index(0);
            app->set_connection_status("");
        }
    });

    app->on_connect_port([&cachedPorts, &portsMutex, &commLoop, app](const slint::SharedString &friendlyName) {
        if (friendlyName.empty()) {
            app->set_connection_status("Select a port before connecting");
            return;
        }

        std::string portPathToConnect;
        std::string friendlyNameStr{friendlyName};

        {
            std::lock_guard<std::mutex> lock(portsMutex);
            for (const auto &portInfo : cachedPorts) {
                if (portInfo.friendlyName == friendlyNameStr) {
                    portPathToConnect = portInfo.portPath;
                    break;
                }
            }
        }

        if (portPathToConnect.empty()) {
            app->set_connection_status("Port not found");
            return;
        }

        GUI_LOG_INFO("Connect", "Connecting to port path: %s (from friendly name: %s)",
                     portPathToConnect.c_str(), friendlyNameStr.c_str());

        commLoop.post(gui::common::CommLoop::ConnectCmd{portPathToConnect});
        app->set_show_connection_screen(false);
        app->set_waiting_for_device_data(true);
        app->set_connection_status("");
    });

    app->on_save_channel_config([&commLoop, app](int channel_idx, slint::SharedString note_str, ModeConfig mode_config) {
        GUI_LOG_INFO("SaveConfig", "Callback invoked for channel %d", channel_idx);

        uint16_t note_number = 255;
        if (!note_str.empty()) {
            std::string note_string{note_str};
            note_number = parseNoteString(note_string);
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
                auto params = std::make_unique<midi2pwm::pwm::InstantModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(mode_config.instant_data.on_level);
                params->velocity_sensitive = mode_config.instant_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::InstantModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 1: {
                auto params = std::make_unique<midi2pwm::pwm::RampedModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(mode_config.ramped_data.on_level);
                params->velocity_sensitive = mode_config.ramped_data.velocity_sensitive;
                params->attack_time_ms = static_cast<uint16_t>(mode_config.ramped_data.attack_time_ms);
                params->release_time_ms = static_cast<uint16_t>(mode_config.ramped_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::RampedModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 2: {
                auto params = std::make_unique<midi2pwm::pwm::PulseModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(mode_config.pulse_data.on_level);
                params->velocity_sensitive = mode_config.pulse_data.velocity_sensitive;
                params->attack_time_ms = static_cast<uint16_t>(mode_config.pulse_data.attack_time_ms);
                params->hold_time_ms = static_cast<uint16_t>(mode_config.pulse_data.hold_time_ms);
                params->release_time_ms = static_cast<uint16_t>(mode_config.pulse_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PulseModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 3: {
                auto params = std::make_unique<midi2pwm::pwm::ToggleModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(mode_config.toggle_data.on_level);
                params->velocity_sensitive = mode_config.toggle_data.velocity_sensitive;
                params->debounce_delay_ms = static_cast<uint16_t>(mode_config.toggle_data.debounce_delay_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ToggleModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 4: {
                auto params = std::make_unique<midi2pwm::pwm::ADSRModeParamsT>();
                params->attack_level = gui::common::uiPercentToDevice(mode_config.adsr_data.attack_level);
                params->sustain_level = gui::common::uiPercentToDevice(mode_config.adsr_data.sustain_level);
                params->velocity_sensitive = mode_config.adsr_data.velocity_sensitive;
                params->attack_time_ms = static_cast<uint16_t>(mode_config.adsr_data.attack_time_ms);
                params->decay_time_ms = static_cast<uint16_t>(mode_config.adsr_data.decay_time_ms);
                params->release_time_ms = static_cast<uint16_t>(mode_config.adsr_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ADSRModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 5: {
                auto params = std::make_unique<midi2pwm::pwm::CCControlModeParamsT>();
                params->cc_number = static_cast<uint8_t>(mode_config.cc_data.cc_number);
                params->center_value = static_cast<uint8_t>(mode_config.cc_data.center_value);
                params->left_max_pwm = gui::common::uiPercentToDevice(mode_config.cc_data.left_max_pwm);
                params->right_max_pwm = gui::common::uiPercentToDevice(mode_config.cc_data.right_max_pwm);
                params->deadband_range = static_cast<uint8_t>(mode_config.cc_data.deadband_range);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::CCControlModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 6: {
                auto params = std::make_unique<midi2pwm::pwm::PitchBendModeParamsT>();
                params->base_level = gui::common::uiPercentToDevice(mode_config.pitchbend_data.base_level);
                params->bend_range = gui::common::uiPercentToDevice(mode_config.pitchbend_data.bend_range);
                params->unipolar = mode_config.pitchbend_data.unipolar;
                params->velocity_sensitive = mode_config.pitchbend_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PitchBendModeParams;
                config.mode_params.value = params.release();
                break;
            }
            default:
                GUI_LOG_WARNING("SaveConfig", "Unsupported mode type: %d", mode_config.mode_type);
                break;
        }

        commLoop.post(gui::common::CommLoop::SendConfigCmd{std::move(config)});
    });

    app->on_assign_note_clicked([&isAssigningNote, &channelAwaitingNote](int channel_idx) {
        GUI_LOG_INFO("Assignment", "Entering note assignment mode for channel %d", channel_idx);
        isAssigningNote.store(true);
        channelAwaitingNote.store(channel_idx);
    });

    app->on_note_assigned_from_backend([app](slint::SharedString note_str) {
        app->set_temp_note(note_str);
        app->set_temp_is_assigning(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_assign_cc_clicked([&isAssigningCc]() {
        GUI_LOG_INFO("Assignment", "Entering CC assignment mode");
        isAssigningCc.store(true);
    });

    app->on_cc_assigned_from_backend([app](int cc_number) {
        auto temp_config = app->get_temp_mode_config();
        temp_config.cc_data.cc_number = cc_number;
        app->set_temp_mode_config(temp_config);
        app->set_temp_is_assigning_cc(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_reset_note_clicked([](int) {});

    app->on_reset_cc_clicked([app]() {
        auto temp_config = app->get_temp_mode_config();
        temp_config.cc_data.cc_number = 0;
        app->set_temp_mode_config(temp_config);
        app->set_temp_popup_dirty(true);
    });

    app->on_popup_closed([&isAssigningNote, &channelAwaitingNote, &isAssigningCc, app]() {
        if (isAssigningNote.load()) {
            isAssigningNote.store(false);
            channelAwaitingNote.store(-1);
        }
        if (isAssigningCc.load()) {
            isAssigningCc.store(false);
            app->set_temp_is_assigning_cc(false);
        }
    });

    app->set_midi_message("No midi message received yet");

    uint32_t lastConnVersion = 0;
    uint32_t lastTelVersion = 0;
    uint32_t lastMidiVersion = 0;
    uint32_t lastOtaProgressVersion = 0;
    std::shared_ptr<slint::VectorModel<ChannelData>> channelModel;

    slint::Timer uiPollTimer(std::chrono::milliseconds(100), [&]() {
        uint32_t connVer = connectionViewModel.stateVersion();
        if (connVer != lastConnVersion) {
            lastConnVersion = connVer;
            bool alive = connectionViewModel.isPeerAlive();

            if (alive) {
                app->set_show_connection_screen(false);
                app->set_waiting_for_device_data(false);
                app->set_reconnecting_overlay_visible(false);
                app->set_connection_status("");
            } else {
                if (connectionViewModel.isConnected()) {
                    app->set_reconnecting_overlay_visible(true);
                } else {
                    app->set_show_connection_screen(true);
                    app->set_waiting_for_device_data(false);
                    app->set_reconnecting_overlay_visible(false);
                    app->set_connection_status("Connection lost. Select a port to reconnect.");
                }
            }
        }

        uint32_t telVer = channelTelemetryViewModel.version();
        if (telVer != lastTelVersion) {
            lastTelVersion = telVer;
            auto channels = channelTelemetryViewModel.channels();

            if (!channelModel) {
                channelModel = std::make_shared<slint::VectorModel<ChannelData>>();
                for (const auto &ch : channels) {
                    channelModel->push_back(toSlintChannelData(ch));
                }
                app->set_channel_model(channelModel);
            } else {
                for (std::size_t i = 0; i < channels.size(); ++i) {
                    channelModel->set_row_data(i, toSlintChannelData(channels[i]));
                }
            }
        }

        uint32_t midiVer = midiViewModel.version();
        if (midiVer != lastMidiVersion) {
            lastMidiVersion = midiVer;
            app->set_midi_message(slint::SharedString(midiViewModel.lastMessage().c_str()));

            auto rawMsg = midiViewModel.lastRawMessage();

            if (isAssigningNote.load()) {
                bool isNoteMessage = (rawMsg.message_type == midi2pwm::midi::ChannelMessageType::NoteOn ||
                                      rawMsg.message_type == midi2pwm::midi::ChannelMessageType::NoteOff);
                if (isNoteMessage && rawMsg.data1 <= 127) {
                    int channelIdx = channelAwaitingNote.exchange(-1);
                    isAssigningNote.store(false);
                    if (channelIdx >= 0) {
                        std::string noteName = midiNoteToString(static_cast<uint16_t>(rawMsg.data1));
                        app->invoke_note_assigned_from_backend(slint::SharedString{noteName.c_str()});
                    }
                }
            } else if (isAssigningCc.load()) {
                bool isCcMessage = (rawMsg.message_type == midi2pwm::midi::ChannelMessageType::ControlChange);
                if (isCcMessage && rawMsg.data1 <= 127) {
                    isAssigningCc.store(false);
                    app->invoke_cc_assigned_from_backend(static_cast<int>(rawMsg.data1));
                }
            }
        }

        uint32_t otaVer = s_otaProgress.version.load(std::memory_order_acquire);
        if (otaVer != lastOtaProgressVersion) {
            lastOtaProgressVersion = otaVer;

            midi2pwm::ota::OtaStatus status;
            uint16_t chunksReceived;
            uint16_t totalChunks;
            std::string errorStr;
            {
                std::lock_guard<std::mutex> lock(s_otaProgress.mutex);
                status = s_otaProgress.status;
                chunksReceived = s_otaProgress.chunksReceived;
                totalChunks = s_otaProgress.totalChunks;
                errorStr = s_otaProgress.errorMessage;
            }

            using Status = midi2pwm::ota::OtaStatus;
            switch (status) {
            case Status::Receiving: {
                float pct = totalChunks > 0
                    ? (static_cast<float>(chunksReceived) / static_cast<float>(totalChunks)) * 100.0f
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
                std::string msg = "Error: " + errorStr;
                app->set_ota_status_text(slint::SharedString(msg.c_str()));
                app->set_ota_is_uploading(false);
                break;
            }
            default:
                break;
            }
        }
    });

    app->run();

    GUI_LOG_INFO("Shutdown", "Application window closed, stopping comm thread");
    commLoop.requestStop();
    commThread.join();

    connectionViewModel.disconnect();

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
