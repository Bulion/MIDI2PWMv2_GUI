#include "app-window.h"
#include "log_forwarder.h"
#include "uart_backend.h"
#include "waveshare_rgb_lcd_port.h"

#include "gui_common/log.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"

#include <cstdlib>
#include <esp_log.h>
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

struct Esp32Controller {
    slint::ComponentHandle<AppWindow> app;
    ConnectionViewModel &connectionViewModel;
    MidiMessageViewModel &midiViewModel;
    std::atomic<bool> isAssigningNote{false};
    std::atomic<int> channelAwaitingNote{-1};
    std::atomic<bool> isAssigningCc{false};

    void OnMidiMessage(const midi2pwm::midi::ChannelMessageT &message)
    {
        bool isNoteMessage = (message.message_type == midi2pwm::midi::ChannelMessageType::NoteOn ||
                              message.message_type == midi2pwm::midi::ChannelMessageType::NoteOff);

        if (isAssigningNote.load()) {
            if (isNoteMessage && message.data1 <= 127) {
                int channelIdx = channelAwaitingNote.exchange(-1);
                isAssigningNote.store(false);

                if (channelIdx >= 0) {
                    std::string noteName = midiNoteToString(static_cast<uint16_t>(message.data1));
                    slint::SharedString noteStr{noteName.c_str()};

                    slint::invoke_from_event_loop([handle = app, noteStr]() {
                        handle->invoke_note_assigned_from_backend(noteStr);
                    });
                }
            }
        } else if (isAssigningCc.load()) {
            bool isCcMessage = (message.message_type == midi2pwm::midi::ChannelMessageType::ControlChange);

            if (isCcMessage && message.data1 <= 127) {
                isAssigningCc.store(false);
                int ccNumber = static_cast<int>(message.data1);

                slint::invoke_from_event_loop([handle = app, ccNumber]() {
                    handle->invoke_cc_assigned_from_backend(ccNumber);
                });
            }
        }

        midiViewModel.updateFromChannelMessage(message);
    }

    void OnMessage(const MidiMessageViewModel::MessageString &text) const
    {
        slint::SharedString shared{text.c_str()};
        slint::invoke_from_event_loop([handle = app, shared]() {
            handle->set_midi_message(shared);
        });
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

    void OnConnectionChanged(bool connected) const
    {
        GUI_LOG_INFO(TAG, "Connection state changed: %s", connected ? "CONNECTED" : "DISCONNECTED");

        if (!connected) {
            GUI_LOG_INFO(TAG, "Auto-reconnecting...");
            connectionViewModel.connect("UART0");
            return;
        }

        slint::invoke_from_event_loop([this, handle = app]() {
            bool fullyConnected = connectionViewModel.isFullyConnected();
            handle->set_show_connection_screen(false);
            handle->set_waiting_for_device_data(!fullyConnected);
            handle->set_connection_status("");
        });
    }

    static std::string midiNoteToString(uint16_t noteNumber)
    {
        static const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int octave = (noteNumber / 12) - 1;
        int noteIdx = noteNumber % 12;
        return std::string(NOTE_NAMES[noteIdx]) + std::to_string(octave);
    }
};

static uint16_t parseNoteString(const std::string &noteStr)
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

} // namespace

extern "C" void app_main(void)
{
    initializeDisplay();
    gui::common::InitializeLibcommLogging();

    auto app = AppWindow::create();

    MidiMessageViewModel midiViewModel;
    ChannelTelemetryViewModel channelTelemetryViewModel;

    esp_log_set_vprintf([](const char *, va_list) -> int { return 0; });

    gui::esp32::UartBackend backend;
    if (!backend.initialize()) {
        abort();
    }

    gui::esp32::initLogForwarder();

    ConnectionViewModel connectionViewModel{backend, midiViewModel, channelTelemetryViewModel};

    Esp32Controller controller{app, connectionViewModel, midiViewModel};

    midiViewModel.setUpdateCallback(
        MidiMessageViewModel::UpdateCallback::create<Esp32Controller, &Esp32Controller::OnMessage>(controller));
    channelTelemetryViewModel.setUpdateCallback(
        ChannelTelemetryViewModel::UpdateCallback::create<Esp32Controller, &Esp32Controller::OnChannelTelemetry>(controller));
    connectionViewModel.setConnectionChangedCallback(
        ConnectionViewModel::ConnectionChangedCallback::create<Esp32Controller, &Esp32Controller::OnConnectionChanged>(controller));
    connectionViewModel.setRawMidiMessageCallback(
        ConnectionViewModel::RawMidiMessageCallback::create<Esp32Controller, &Esp32Controller::OnMidiMessage>(controller));

    if (!connectionViewModel.connect("UART0")) {
        GUI_LOG_ERROR(TAG, "Failed to connect via UART backend");
    }

    app->on_save_channel_config([&connectionViewModel, app](int channelIdx, slint::SharedString noteStr, ModeConfig modeConfig) {
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

        bool success = connectionViewModel.sendChannelConfig(config);
        if (!success) {
            GUI_LOG_ERROR(TAG, "Failed to send channel config for channel %d", channelIdx);
        }
    });

    app->on_assign_note_clicked([&controller](int channelIdx) {
        controller.isAssigningNote.store(true);
        controller.channelAwaitingNote.store(channelIdx);
    });

    app->on_note_assigned_from_backend([app](slint::SharedString noteStr) {
        app->set_temp_note(noteStr);
        app->set_temp_is_assigning(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_assign_cc_clicked([&controller]() {
        controller.isAssigningCc.store(true);
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

    app->on_popup_closed([&controller, app]() {
        if (controller.isAssigningNote.load()) {
            controller.isAssigningNote.store(false);
            controller.channelAwaitingNote.store(-1);
        }
        if (controller.isAssigningCc.load()) {
            controller.isAssigningCc.store(false);
            app->set_temp_is_assigning_cc(false);
        }
    });

    app->on_refresh_ports([]() {});
    app->on_connect_port([](const slint::SharedString &) {});

    app->set_show_connection_screen(false);
    app->set_waiting_for_device_data(true);
    app->set_midi_message("No midi message received yet");

    static bool wasFullyConnected = false;
    slint::Timer updateTimer(std::chrono::milliseconds(100), [&connectionViewModel, app]() {
        connectionViewModel.update();

        bool fullyConnected = connectionViewModel.isFullyConnected();
        if (fullyConnected != wasFullyConnected) {
            wasFullyConnected = fullyConnected;
            app->set_waiting_for_device_data(!fullyConnected);
        }
    });

    app->run();
}
