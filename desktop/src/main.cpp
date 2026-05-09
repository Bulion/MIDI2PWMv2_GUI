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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using gui::common::ChannelTelemetryViewModel;
using gui::common::ConnectionViewModel;
using gui::common::MidiMessageViewModel;

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
    std::atomic<bool> isAssigningNoteB{false};
    std::atomic<int> channelAwaitingNoteB{-1};
    std::atomic<bool> isAssigningCc{false};

    std::atomic<uint32_t> portsVersion{0};
    std::vector<gui::common::PortInfo> cachedPorts;
    std::mutex portsMutex;

    std::thread commThread([&commLoop]() {
        while (!commLoop.shouldStop()) {
            commLoop.runOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
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
        uint16_t noteNumberB = 255;
        std::string noteBStr{modeConfig.note_b};
        if (!noteBStr.empty()) {
            noteNumberB = gui::common::parseNoteString(noteBStr);
        }
        auto config = gui::common::buildChannelConfigFromModeConfig(channelIdx, noteNumber, noteNumberB, modeConfig);
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

    app->on_assign_note_b_clicked([&isAssigningNoteB, &channelAwaitingNoteB](int channel_idx) {
        GUI_LOG_INFO("Assignment", "Entering note B assignment mode for channel %d", channel_idx);
        isAssigningNoteB.store(true);
        channelAwaitingNoteB.store(channel_idx);
    });

    app->on_note_b_assigned_from_backend([app](slint::SharedString note_str) {
        app->set_temp_note_b(note_str);
        app->set_temp_is_assigning_b(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_reset_note_b_clicked([](int) {});

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

    app->on_reset_fault_clicked([&commLoop](int channelIdx) {
        commLoop.post(gui::common::CommLoop::ResetFaultCmd{static_cast<std::uint16_t>(channelIdx)});
    });

    app->on_popup_closed([&isAssigningNote, &channelAwaitingNote, &isAssigningNoteB, &channelAwaitingNoteB, &isAssigningCc, app]() {
        if (isAssigningNote.load()) {
            isAssigningNote.store(false);
            channelAwaitingNote.store(-1);
        }
        if (isAssigningNoteB.load()) {
            isAssigningNoteB.store(false);
            channelAwaitingNoteB.store(-1);
        }
        if (isAssigningCc.load()) {
            isAssigningCc.store(false);
            app->set_temp_is_assigning_cc(false);
        }
    });

    app->set_show_connection_screen(true);
    app->set_midi_message("No midi message received yet");

    uint32_t lastConnVersion = 0;
    uint32_t lastTelVersion = 0;
    uint32_t lastMidiVersion = 0;
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

        if (!channelModel) {
            channelModel = std::make_shared<slint::VectorModel<ChannelData>>();
            auto channels = channelTelemetryViewModel.channels();
            for (const auto &ch : channels) {
                channelModel->push_back(gui::common::toSlintChannelData(ch));
            }
            app->set_channel_model(channelModel);
        } else {
            for (std::size_t i = 0; i < 16; ++i) {
                if (channelTelemetryViewModel.isChannelDirty(i)) {
                    channelModel->set_row_data(i, gui::common::toSlintChannelData(channelTelemetryViewModel.channel(i)));
                    channelTelemetryViewModel.clearChannelDirty(i);
                }
            }
        }

        if (channelTelemetryViewModel.isGlobalDirty()) {
            app->set_input_voltage(channelTelemetryViewModel.inputVoltage());
            app->set_total_current(channelTelemetryViewModel.totalCurrentAmps());
            channelTelemetryViewModel.clearGlobalDirty();
        }

        uint32_t midiVer = midiViewModel.version();
        if (midiVer != lastMidiVersion) {
            lastMidiVersion = midiVer;
            app->set_midi_message(slint::SharedString(midiViewModel.lastMessage().c_str()));
            app->set_midi_type(slint::SharedString(midiViewModel.lastTypeLine().c_str()));
            app->set_midi_data(slint::SharedString(midiViewModel.lastDataLine().c_str()));

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
            } else if (isAssigningNoteB.load()) {
                bool isNoteMessage = (rawMsg.message_type == midi2pwm::midi::ChannelMessageType::NoteOn ||
                                      rawMsg.message_type == midi2pwm::midi::ChannelMessageType::NoteOff);
                if (isNoteMessage && rawMsg.data1 <= 127) {
                    int channelIdx = channelAwaitingNoteB.exchange(-1);
                    isAssigningNoteB.store(false);
                    if (channelIdx >= 0) {
                        std::string noteName = gui::common::midiNoteToString(static_cast<uint16_t>(rawMsg.data1));
                        app->invoke_note_b_assigned_from_backend(slint::SharedString{noteName.c_str()});
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

    });

    app->run();

    GUI_LOG_INFO("Shutdown", "Application window closed, stopping comm thread");
    commLoop.requestStop();
    commThread.join();

    connectionViewModel.disconnect();

    GUI_LOG_INFO("Shutdown", "Cleanup complete, exiting");
    return 0;
}
