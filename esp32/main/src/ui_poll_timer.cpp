#include "ui_poll_timer.h"

#include "app-window.h"
#include "gui_common/log.h"
#include "gui_common/midi_note_util.h"
#include "gui_common/slint_adapters.h"
#include "ota_handler.h"

#include "midi_messages_generated.h"

#include <cstdio>
#include <esp_timer.h>
#include <memory>

namespace gui::esp32
{

namespace
{

constexpr const char *TAG = "UiPoll";

std::unique_ptr<slint::Timer> s_uiPollTimer;

} // namespace

void startUiPollTimer(slint::ComponentHandle<AppWindow> app,
                      gui::common::CommLoop &commLoop,
                      gui::common::ConnectionViewModel &connVm,
                      gui::common::ChannelTelemetryViewModel &telVm,
                      gui::common::MidiMessageViewModel &midiVm,
                      MidiAssignmentState &assignState)
{
    uint32_t lastConnVersion = 0;
    uint32_t lastTelVersion = 0;
    uint32_t lastMidiVersion = 0;
    int64_t lastOtaUpdateUs = 0;
    int64_t otaErrorShownAtUs = 0;
    uint32_t lastOtaDisplayVersion = 0;
    std::shared_ptr<slint::VectorModel<ChannelData>> channelModel;

    s_uiPollTimer = std::make_unique<slint::Timer>(std::chrono::milliseconds(100),
        [app, &commLoop, &connVm, &telVm, &midiVm, &assignState,
         lastConnVersion, lastTelVersion, lastMidiVersion,
         lastOtaUpdateUs, otaErrorShownAtUs, lastOtaDisplayVersion,
         channelModel]() mutable {

        uint32_t connVer = connVm.stateVersion();
        if (connVer != lastConnVersion) {
            lastConnVersion = connVer;
            bool alive = connVm.isPeerAlive();

            if (alive) {
                app->set_show_connection_screen(false);
                app->set_waiting_for_device_data(false);
                app->set_reconnecting_overlay_visible(false);
                app->set_connection_status("");
            } else {
                app->set_reconnecting_overlay_visible(true);
                if (!connVm.isConnected()) {
                    GUI_LOG_INFO(TAG, "Backend disconnected, auto-reconnecting...");
                    commLoop.post(gui::common::CommLoop::ConnectCmd{"UART0"});
                }
            }
        }

        uint32_t telVer = telVm.version();
        if (telVer != lastTelVersion) {
            lastTelVersion = telVer;
            auto channels = telVm.channels();

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

        uint32_t midiVer = midiVm.version();
        if (midiVer != lastMidiVersion) {
            lastMidiVersion = midiVer;
            app->set_midi_message(slint::SharedString(midiVm.lastMessage().c_str()));

            auto rawMsg = midiVm.lastRawMessage();

            if (assignState.isAssigningNote.load()) {
                bool isNoteMessage = (rawMsg.message_type == midi2pwm::midi::ChannelMessageType::NoteOn ||
                                      rawMsg.message_type == midi2pwm::midi::ChannelMessageType::NoteOff);
                if (isNoteMessage && rawMsg.data1 <= 127) {
                    int channelIdx = assignState.channelAwaitingNote.exchange(-1);
                    assignState.isAssigningNote.store(false);
                    if (channelIdx >= 0) {
                        std::string noteName = gui::common::midiNoteToString(static_cast<uint16_t>(rawMsg.data1));
                        app->invoke_note_assigned_from_backend(slint::SharedString{noteName.c_str()});
                    }
                }
            } else if (assignState.isAssigningCc.load()) {
                bool isCcMessage = (rawMsg.message_type == midi2pwm::midi::ChannelMessageType::ControlChange);
                if (isCcMessage && rawMsg.data1 <= 127) {
                    assignState.isAssigningCc.store(false);
                    app->invoke_cc_assigned_from_backend(static_cast<int>(rawMsg.data1));
                }
            }
        }

        constexpr int64_t OTA_POLL_INTERVAL_US = 500'000;
        constexpr int64_t OTA_ERROR_DISPLAY_US = 5'000'000;

        auto &otaDisplay = otaDisplayState();
        int64_t nowUs = esp_timer_get_time();
        uint32_t otaVer = otaDisplay.version.load(std::memory_order_acquire);
        bool otaVersionChanged = otaVer != lastOtaDisplayVersion;
        bool otaPollDue = (nowUs - lastOtaUpdateUs) >= OTA_POLL_INTERVAL_US;
        bool otaNeedsUpdate = otaVersionChanged && (otaPollDue || otaDisplay.status != midi2pwm::ota::OtaStatus::Receiving);

        if (otaNeedsUpdate) {
            lastOtaDisplayVersion = otaVer;
            lastOtaUpdateUs = nowUs;

            midi2pwm::ota::OtaStatus otaStatus;
            uint16_t chunks = 0;
            uint16_t total = 0;
            uint32_t compressedSize = 0;
            {
                std::lock_guard lock(otaDisplay.mutex);
                otaStatus = otaDisplay.status;
                chunks = otaDisplay.chunksReceived;
                total = otaDisplay.totalChunks;
                compressedSize = otaDisplay.compressedSize;
            }

            bool showScreen = (otaStatus != midi2pwm::ota::OtaStatus::Idle);
            app->set_ota_screen_visible(showScreen);

            if (showScreen) {
                app->set_ota_screen_status(static_cast<int>(otaStatus));

                using Status = midi2pwm::ota::OtaStatus;
                switch (otaStatus) {
                case Status::Preparing:
                    app->set_ota_screen_phase_text(slint::SharedString("Preparing..."));
                    app->set_ota_screen_detail_text(slint::SharedString("Allocating PSRAM buffer"));
                    app->set_ota_screen_progress(0);
                    break;
                case Status::Receiving: {
                    float pct = total > 0 ? 100.f * chunks / total : 0.f;
                    uint32_t kbReceived = static_cast<uint32_t>(chunks);
                    uint32_t kbTotal = compressedSize / 1024;
                    char detail[32];
                    std::snprintf(detail, sizeof(detail), "%lu / %lu KB",
                                  static_cast<unsigned long>(kbReceived),
                                  static_cast<unsigned long>(kbTotal));
                    app->set_ota_screen_phase_text(slint::SharedString("Downloading firmware..."));
                    app->set_ota_screen_detail_text(slint::SharedString(detail));
                    app->set_ota_screen_progress(pct);
                    break;
                }
                case Status::Verifying:
                    app->set_ota_screen_phase_text(slint::SharedString("Verifying firmware..."));
                    app->set_ota_screen_detail_text(slint::SharedString(""));
                    app->set_ota_screen_progress(100);
                    break;
                case Status::Applying:
                    app->set_ota_screen_phase_text(slint::SharedString("Writing to flash..."));
                    app->set_ota_screen_detail_text(slint::SharedString(""));
                    app->set_ota_screen_progress(100);
                    break;
                case Status::Rebooting:
                    app->set_ota_screen_phase_text(slint::SharedString("Rebooting..."));
                    app->set_ota_screen_detail_text(slint::SharedString(""));
                    app->set_ota_screen_progress(100);
                    break;
                case Status::Error:
                    app->set_ota_screen_phase_text(slint::SharedString("Update failed"));
                    app->set_ota_screen_error(slint::SharedString(""));
                    otaErrorShownAtUs = nowUs;
                    break;
                default:
                    break;
                }
            }
        }

        if (app->get_ota_screen_visible() && app->get_ota_screen_status() == static_cast<int>(midi2pwm::ota::OtaStatus::Error)) {
            if (otaErrorShownAtUs > 0 && (nowUs - otaErrorShownAtUs) > OTA_ERROR_DISPLAY_US) {
                app->set_ota_screen_visible(false);
                {
                    std::lock_guard lock(otaDisplay.mutex);
                    otaDisplay.status = midi2pwm::ota::OtaStatus::Idle;
                }
                otaErrorShownAtUs = 0;
            }
        }
    });
}

} // namespace gui::esp32
