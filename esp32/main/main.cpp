#include "app-window.h"
#include "esp32_flash_writer.h"
#include "log_forwarder.h"
#include "psram_buffered_flash_writer.h"
#include "uart_backend.h"
#include "waveshare_rgb_lcd_port.h"

#include "gui_common/comm_loop.h"
#include "gui_common/log.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"

#include "libcomm/frame_transport.h"
#include "libcomm/ota_manager.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mutex>
#include <slint-esp.h>
#include <slint.h>
#include <span>

using gui::common::ChannelTelemetryViewModel;
using gui::common::ConnectionViewModel;
using gui::common::MidiMessageViewModel;

namespace
{

constexpr const char *TAG = "Main";

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

uint16_t parseNoteString(const std::string &noteStr)
{
    if (noteStr.empty() || noteStr == "---") {
        return 255;
    }

    static const std::pair<const char *, int> NOTE_MAP[] = {
        {"C#", 1}, {"D#", 3}, {"F#", 6}, {"G#", 8}, {"A#", 10},
        {"C", 0}, {"D", 2}, {"E", 4}, {"F", 5}, {"G", 7}, {"A", 9}, {"B", 11}
    };

    size_t octavePos = noteStr.find_first_of("0123456789-");
    if (octavePos == std::string::npos || octavePos == 0) {
        return 255;
    }

    std::string noteName = noteStr.substr(0, octavePos);
    int octave = std::atoi(noteStr.c_str() + octavePos);

    int noteIdx = -1;
    for (const auto &[name, idx] : NOTE_MAP) {
        if (noteName == name) {
            noteIdx = idx;
            break;
        }
    }

    if (noteIdx < 0) {
        return 255;
    }

    int midiNote = (octave + 1) * 12 + noteIdx;
    if (midiNote < 0 || midiNote > 127) {
        return 255;
    }

    return static_cast<uint16_t>(midiNote);
}

static Esp32FlashWriter s_esp32FlashWriterDelegate;
static PsramBufferedFlashWriter s_psramFlashWriter(s_esp32FlashWriterDelegate);
static gui::common::ConnectionViewModel *s_otaConnectionVm = nullptr;
static gui::esp32::UartBackend *s_echoBackend = nullptr;

static bool echoWrite(const std::uint8_t *data, std::size_t size)
{
    return s_echoBackend ? s_echoBackend->write(data, size) : false;
}

static libcomm::FrameTransport s_echoTransport(
    libcomm::FrameTransport::WriteCallback::create<&echoWrite>());

static bool echoFrame(const std::uint8_t *data, std::size_t size)
{
    return s_echoTransport.Send(data, size);
}

struct OtaDisplayState {
    std::atomic<uint32_t> version{0};
    std::mutex mutex;
    midi2pwm::ota::OtaStatus status{midi2pwm::ota::OtaStatus::Idle};
    uint16_t chunksReceived{0};
    uint16_t totalChunks{0};
    uint32_t compressedSize{0};
    uint32_t firmwareSize{0};
};

static OtaDisplayState s_otaDisplay;

static void otaSendProgress(midi2pwm::ota::Target target, midi2pwm::ota::OtaStatus status,
                            std::uint16_t chunksReceived, std::uint16_t totalChunks,
                            const char *errorMessage)
{
    {
        std::lock_guard lock(s_otaDisplay.mutex);
        s_otaDisplay.status = status;
        s_otaDisplay.chunksReceived = chunksReceived;
        s_otaDisplay.totalChunks = totalChunks;
    }
    s_otaDisplay.version.fetch_add(1, std::memory_order_release);

    if (s_otaConnectionVm) {
        s_otaConnectionVm->messageProcessor().sendOtaProgress(
            target, status, chunksReceived, totalChunks, errorMessage);
    }
}

static libcomm::OtaManager s_otaManager(
    s_psramFlashWriter,
    midi2pwm::ota::Target::Esp32,
    libcomm::OtaManager::SendProgressCallback::create<&otaSendProgress>());

static constexpr uint32_t OTA_DATA_TIMEOUT_MS = 30000;
static std::atomic<int64_t> s_lastOtaActivityUs{0};

static void otaTouchActivity()
{
    s_lastOtaActivityUs.store(esp_timer_get_time(), std::memory_order_release);
}

static void onOtaBegin(const midi2pwm::ota::OtaBegin &msg)
{
    otaTouchActivity();
    {
        std::lock_guard lock(s_otaDisplay.mutex);
        s_otaDisplay.compressedSize = msg.compressed_size();
        s_otaDisplay.firmwareSize = msg.firmware_size();
        s_otaDisplay.totalChunks = msg.total_chunks();
    }
    s_psramFlashWriter.setCompressedTransfer(
        msg.compressed_size(), msg.compressed_crc32(), msg.firmware_size());
    s_otaManager.handleBegin(msg);
}

static void onOtaData(const midi2pwm::ota::OtaData &msg)
{
    otaTouchActivity();
    s_otaManager.handleData(msg);
    s_otaDisplay.chunksReceived = msg.chunk_index() + 1;
    s_otaDisplay.version.fetch_add(1, std::memory_order_release);
}

static void otaEndTask(void *)
{
    s_otaManager.handleEnd();
    if (s_otaManager.status() == midi2pwm::ota::OtaStatus::Rebooting) {
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(2000));
        esp_restart();
    }
    vTaskDelete(nullptr);
}

static void onOtaEnd(const midi2pwm::ota::OtaEnd &)
{
    otaTouchActivity();
    xTaskCreatePinnedToCore(otaEndTask, "ota_end", 8192, nullptr, 3, nullptr, 1);
}

static void onOtaAbort(const midi2pwm::ota::OtaAbort &msg)
{
    s_otaManager.handleAbort(msg);
    s_lastOtaActivityUs.store(0, std::memory_order_release);
    {
        std::lock_guard lock(s_otaDisplay.mutex);
        s_otaDisplay.status = midi2pwm::ota::OtaStatus::Error;
    }
    s_otaDisplay.version.fetch_add(1, std::memory_order_release);
}

static void checkOtaDataTimeout()
{
    auto status = s_otaManager.status();
    if (status != midi2pwm::ota::OtaStatus::Receiving) {
        return;
    }

    int64_t lastActivity = s_lastOtaActivityUs.load(std::memory_order_acquire);
    if (lastActivity == 0) {
        return;
    }

    int64_t nowUs = esp_timer_get_time();
    int64_t elapsedMs = (nowUs - lastActivity) / 1000;
    if (elapsedMs > OTA_DATA_TIMEOUT_MS) {
        GUI_LOG_ERROR(TAG, "OTA data timeout after %lld ms", static_cast<long long>(elapsedMs));
        s_otaManager.handleAbort("Data reception timeout");
        s_lastOtaActivityUs.store(0, std::memory_order_release);
    }
}

static void commTaskFn(void *param)
{
    auto *loop = static_cast<gui::common::CommLoop *>(param);
    while (!loop->shouldStop()) {
        loop->runOnce();
        checkOtaDataTimeout();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(nullptr);
}

} // namespace

extern "C" void app_main(void)
{
    initializeDisplay();
    gui::common::InitializeLibcommLogging();

    auto app = AppWindow::create();

    MidiMessageViewModel midiViewModel;
    ChannelTelemetryViewModel channelTelemetryViewModel;

    gui::esp32::UartBackend backend;
    if (!backend.initialize()) {
        abort();
    }

    gui::esp32::initLogForwarder(true);

    ConnectionViewModel connectionViewModel{backend, midiViewModel, channelTelemetryViewModel};

    gui::common::CommLoop commLoop(connectionViewModel);

    s_otaConnectionVm = &connectionViewModel;
    s_echoBackend = &backend;

    auto &msgProc = connectionViewModel.messageProcessor();
    msgProc.setUnknownFrameCallback(
        gui::common::MessageProcessor::UnknownFrameCallback::create<&echoFrame>());

    msgProc.setOtaBeginCallback(
        gui::common::MessageProcessor::OtaBeginCallback::create<&onOtaBegin>());
    msgProc.setOtaDataCallback(
        gui::common::MessageProcessor::OtaDataCallback::create<&onOtaData>());
    msgProc.setOtaEndCallback(
        gui::common::MessageProcessor::OtaEndCallback::create<&onOtaEnd>());
    msgProc.setOtaAbortCallback(
        gui::common::MessageProcessor::OtaAbortCallback::create<&onOtaAbort>());

    xTaskCreatePinnedToCore(commTaskFn, "comm", 4096, &commLoop, 4, nullptr, 0);

    if (!connectionViewModel.connect("UART0")) {
        GUI_LOG_ERROR(TAG, "Failed to connect via UART backend");
    }

    std::atomic<bool> isAssigningNote{false};
    std::atomic<int> channelAwaitingNote{-1};
    std::atomic<bool> isAssigningCc{false};

    app->on_save_channel_config([&commLoop](int channelIdx, slint::SharedString noteStr, ModeConfig modeConfig) {
        uint16_t noteNumber = 255;
        if (!noteStr.empty()) {
            noteNumber = parseNoteString(std::string{noteStr});
        }

        midi2pwm::pwm::ChannelConfigT config;
        config.channel_number = static_cast<uint16_t>(channelIdx);
        config.configuration = midi2pwm::pwm::ChannelConfiguration::FullBridge;
        config.note = noteNumber;
        config.min_point = 0.0F;
        config.midpoint = 0.0F;
        config.max_point = 0.0F;
        config.output_mode = static_cast<midi2pwm::pwm::OutputModeType>(modeConfig.mode_type);

        switch (modeConfig.mode_type) {
            case 0: {
                auto params = std::make_unique<midi2pwm::pwm::InstantModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(modeConfig.instant_data.on_level);
                params->velocity_sensitive = modeConfig.instant_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::InstantModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 1: {
                auto params = std::make_unique<midi2pwm::pwm::RampedModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(modeConfig.ramped_data.on_level);
                params->velocity_sensitive = modeConfig.ramped_data.velocity_sensitive;
                params->attack_time_ms = static_cast<uint16_t>(modeConfig.ramped_data.attack_time_ms);
                params->release_time_ms = static_cast<uint16_t>(modeConfig.ramped_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::RampedModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 2: {
                auto params = std::make_unique<midi2pwm::pwm::PulseModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(modeConfig.pulse_data.on_level);
                params->velocity_sensitive = modeConfig.pulse_data.velocity_sensitive;
                params->attack_time_ms = static_cast<uint16_t>(modeConfig.pulse_data.attack_time_ms);
                params->hold_time_ms = static_cast<uint16_t>(modeConfig.pulse_data.hold_time_ms);
                params->release_time_ms = static_cast<uint16_t>(modeConfig.pulse_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PulseModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 3: {
                auto params = std::make_unique<midi2pwm::pwm::ToggleModeParamsT>();
                params->on_level = gui::common::uiPercentToDevice(modeConfig.toggle_data.on_level);
                params->velocity_sensitive = modeConfig.toggle_data.velocity_sensitive;
                params->debounce_delay_ms = static_cast<uint16_t>(modeConfig.toggle_data.debounce_delay_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ToggleModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 4: {
                auto params = std::make_unique<midi2pwm::pwm::ADSRModeParamsT>();
                params->attack_level = gui::common::uiPercentToDevice(modeConfig.adsr_data.attack_level);
                params->sustain_level = gui::common::uiPercentToDevice(modeConfig.adsr_data.sustain_level);
                params->velocity_sensitive = modeConfig.adsr_data.velocity_sensitive;
                params->attack_time_ms = static_cast<uint16_t>(modeConfig.adsr_data.attack_time_ms);
                params->decay_time_ms = static_cast<uint16_t>(modeConfig.adsr_data.decay_time_ms);
                params->release_time_ms = static_cast<uint16_t>(modeConfig.adsr_data.release_time_ms);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ADSRModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 5: {
                auto params = std::make_unique<midi2pwm::pwm::CCControlModeParamsT>();
                params->cc_number = static_cast<uint8_t>(modeConfig.cc_data.cc_number);
                params->center_value = static_cast<uint8_t>(modeConfig.cc_data.center_value);
                params->left_max_pwm = gui::common::uiPercentToDevice(modeConfig.cc_data.left_max_pwm);
                params->right_max_pwm = gui::common::uiPercentToDevice(modeConfig.cc_data.right_max_pwm);
                params->deadband_range = static_cast<uint8_t>(modeConfig.cc_data.deadband_range);
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::CCControlModeParams;
                config.mode_params.value = params.release();
                break;
            }
            case 6: {
                auto params = std::make_unique<midi2pwm::pwm::PitchBendModeParamsT>();
                params->base_level = gui::common::uiPercentToDevice(modeConfig.pitchbend_data.base_level);
                params->bend_range = gui::common::uiPercentToDevice(modeConfig.pitchbend_data.bend_range);
                params->unipolar = modeConfig.pitchbend_data.unipolar;
                params->velocity_sensitive = modeConfig.pitchbend_data.velocity_sensitive;
                config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PitchBendModeParams;
                config.mode_params.value = params.release();
                break;
            }
            default:
                GUI_LOG_WARNING(TAG, "Unsupported mode type: %d", modeConfig.mode_type);
                break;
        }

        commLoop.post(gui::common::CommLoop::SendConfigCmd{std::move(config)});
    });

    app->on_assign_note_clicked([&isAssigningNote, &channelAwaitingNote](int channelIdx) {
        isAssigningNote.store(true);
        channelAwaitingNote.store(channelIdx);
    });

    app->on_note_assigned_from_backend([app](slint::SharedString noteStr) {
        app->set_temp_note(noteStr);
        app->set_temp_is_assigning(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_assign_cc_clicked([&isAssigningCc]() {
        isAssigningCc.store(true);
    });

    app->on_cc_assigned_from_backend([app](int ccNumber) {
        auto tempConfig = app->get_temp_mode_config();
        tempConfig.cc_data.cc_number = ccNumber;
        app->set_temp_mode_config(tempConfig);
        app->set_temp_is_assigning_cc(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_reset_note_clicked([](int) {});
    app->on_reset_cc_clicked([app]() {
        auto tempConfig = app->get_temp_mode_config();
        tempConfig.cc_data.cc_number = 0;
        app->set_temp_mode_config(tempConfig);
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

    app->on_refresh_ports([]() {});
    app->on_connect_port([](const slint::SharedString &) {});

    app->set_show_connection_screen(false);
    app->set_waiting_for_device_data(true);
    app->set_midi_message("No midi message received yet");

    uint32_t lastConnVersion = 0;
    uint32_t lastTelVersion = 0;
    uint32_t lastMidiVersion = 0;
    int64_t lastOtaUpdateUs = 0;
    int64_t otaErrorShownAtUs = 0;
    uint32_t lastOtaDisplayVersion = 0;
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
                app->set_reconnecting_overlay_visible(true);
                if (!connectionViewModel.isConnected()) {
                    GUI_LOG_INFO(TAG, "Backend disconnected, auto-reconnecting...");
                    commLoop.post(gui::common::CommLoop::ConnectCmd{"UART0"});
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

        constexpr int64_t OTA_POLL_INTERVAL_US = 500'000;
        constexpr int64_t OTA_ERROR_DISPLAY_US = 5'000'000;

        int64_t nowUs = esp_timer_get_time();
        uint32_t otaVer = s_otaDisplay.version.load(std::memory_order_acquire);
        bool otaVersionChanged = otaVer != lastOtaDisplayVersion;
        bool otaPollDue = (nowUs - lastOtaUpdateUs) >= OTA_POLL_INTERVAL_US;
        bool otaNeedsUpdate = otaVersionChanged && (otaPollDue || s_otaDisplay.status != midi2pwm::ota::OtaStatus::Receiving);

        if (otaNeedsUpdate) {
            lastOtaDisplayVersion = otaVer;
            lastOtaUpdateUs = nowUs;

            midi2pwm::ota::OtaStatus otaStatus;
            uint16_t chunks = 0;
            uint16_t total = 0;
            uint32_t compressedSize = 0;
            {
                std::lock_guard lock(s_otaDisplay.mutex);
                otaStatus = s_otaDisplay.status;
                chunks = s_otaDisplay.chunksReceived;
                total = s_otaDisplay.totalChunks;
                compressedSize = s_otaDisplay.compressedSize;
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
                    std::lock_guard lock(s_otaDisplay.mutex);
                    s_otaDisplay.status = midi2pwm::ota::OtaStatus::Idle;
                }
                otaErrorShownAtUs = 0;
            }
        }
    });

    app->run();
}
