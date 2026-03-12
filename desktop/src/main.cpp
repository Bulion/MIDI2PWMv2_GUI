#include <slint.h>

#include "app-window.h"
#include "gui_common/comm_loop.h"
#include "gui_common/log.h"
#include "gui_common/midi_note_util.h"
#include "gui_common/slint_adapters.h"
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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using gui::common::ChannelTelemetryViewModel;
using gui::common::ConnectionViewModel;
using gui::common::MidiMessageViewModel;

namespace
{

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

    app->on_save_channel_config([&commLoop](int channelIdx, slint::SharedString noteStr, ModeConfig modeConfig) {
        uint16_t noteNumber = 255;
        if (!noteStr.empty()) {
            noteNumber = gui::common::parseNoteString(std::string{noteStr});
        }
        auto config = gui::common::buildChannelConfigFromModeConfig(channelIdx, noteNumber, modeConfig);
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
                    channelModel->push_back(gui::common::toSlintChannelData(ch));
                }
                app->set_channel_model(channelModel);
            } else {
                for (std::size_t i = 0; i < channels.size(); ++i) {
                    channelModel->set_row_data(i, gui::common::toSlintChannelData(channels[i]));
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
                        std::string noteName = gui::common::midiNoteToString(static_cast<uint16_t>(rawMsg.data1));
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
