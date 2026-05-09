#pragma once

#include "app-window.h"
#include "gui_common/log.h"
#include "gui_common/midi_note_util.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"

#include "pwm_messages_generated.h"

#include <memory>

namespace gui::common
{

inline ::ChannelData toSlintChannelData(const gui::common::ChannelData &channelData)
{
    ::ChannelData slintChannelData;
    slintChannelData.note = slint::SharedString(channelData.note.c_str());
    slintChannelData.note_number = channelData.noteNumber;
    slintChannelData.voltage = channelData.voltage;
    slintChannelData.current = channelData.currentMa;
    slintChannelData.duty_cycle = channelData.dutyCyclePercent;
    slintChannelData.is_active = channelData.isActive;
    slintChannelData.has_fault = channelData.hasFault;
    slintChannelData.mode_type = channelData.mode_type;
    slintChannelData.polarity = channelData.polarity;
    slintChannelData.release_action = channelData.release_action;
    slintChannelData.configuration = channelData.configuration;
    slintChannelData.note_b = slint::SharedString(channelData.noteB.c_str());
    slintChannelData.note_number_b = channelData.noteNumberB;
    slintChannelData.is_active_a = channelData.isActiveA;
    slintChannelData.is_active_b = channelData.isActiveB;
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

inline midi2pwm::pwm::ChannelConfigT buildChannelConfigFromModeConfig(
    int channelIdx, uint16_t noteNumber, uint16_t noteNumberB, const ModeConfig &modeConfig)
{
    midi2pwm::pwm::ChannelConfigT config;
    config.channel_number = static_cast<uint16_t>(channelIdx);
    config.configuration = static_cast<midi2pwm::pwm::ChannelConfiguration>(modeConfig.configuration);
    config.note = noteNumber;
    config.note_b = noteNumberB;
    config.min_point = 0.0F;
    config.midpoint = 0.0F;
    config.max_point = 0.0F;
    config.output_mode = static_cast<midi2pwm::pwm::OutputModeType>(modeConfig.mode_type);
    config.polarity = static_cast<midi2pwm::pwm::Polarity>(modeConfig.polarity);
    config.release_action = static_cast<midi2pwm::pwm::ReleaseAction>(modeConfig.release_action);

    switch (modeConfig.mode_type) {
        case 0: {
            auto params = std::make_unique<midi2pwm::pwm::InstantModeParamsT>();
            params->on_level = uiPercentToDevice(modeConfig.instant_data.on_level);
            params->velocity_sensitive = modeConfig.instant_data.velocity_sensitive;
            config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::InstantModeParams;
            config.mode_params.value = params.release();
            break;
        }
        case 1: {
            auto params = std::make_unique<midi2pwm::pwm::RampedModeParamsT>();
            params->on_level = uiPercentToDevice(modeConfig.ramped_data.on_level);
            params->velocity_sensitive = modeConfig.ramped_data.velocity_sensitive;
            params->attack_time_ms = static_cast<uint16_t>(modeConfig.ramped_data.attack_time_ms);
            params->release_time_ms = static_cast<uint16_t>(modeConfig.ramped_data.release_time_ms);
            config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::RampedModeParams;
            config.mode_params.value = params.release();
            break;
        }
        case 2: {
            auto params = std::make_unique<midi2pwm::pwm::PulseModeParamsT>();
            params->on_level = uiPercentToDevice(modeConfig.pulse_data.on_level);
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
            params->on_level = uiPercentToDevice(modeConfig.toggle_data.on_level);
            params->velocity_sensitive = modeConfig.toggle_data.velocity_sensitive;
            params->debounce_delay_ms = static_cast<uint16_t>(modeConfig.toggle_data.debounce_delay_ms);
            config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::ToggleModeParams;
            config.mode_params.value = params.release();
            break;
        }
        case 4: {
            auto params = std::make_unique<midi2pwm::pwm::ADSRModeParamsT>();
            params->attack_level = uiPercentToDevice(modeConfig.adsr_data.attack_level);
            params->sustain_level = uiPercentToDevice(modeConfig.adsr_data.sustain_level);
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
            params->left_max_pwm = uiPercentToDevice(modeConfig.cc_data.left_max_pwm);
            params->right_max_pwm = uiPercentToDevice(modeConfig.cc_data.right_max_pwm);
            params->deadband_range = static_cast<uint8_t>(modeConfig.cc_data.deadband_range);
            config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::CCControlModeParams;
            config.mode_params.value = params.release();
            break;
        }
        case 6: {
            auto params = std::make_unique<midi2pwm::pwm::PitchBendModeParamsT>();
            params->base_level = uiPercentToDevice(modeConfig.pitchbend_data.base_level);
            params->bend_range = uiPercentToDevice(modeConfig.pitchbend_data.bend_range);
            params->unipolar = modeConfig.pitchbend_data.unipolar;
            params->velocity_sensitive = modeConfig.pitchbend_data.velocity_sensitive;
            config.mode_params.type = midi2pwm::pwm::ModeParametersUnion::PitchBendModeParams;
            config.mode_params.value = params.release();
            break;
        }
        default:
            GUI_LOG_WARNING("BuildConfig", "Unsupported mode type: %d", modeConfig.mode_type);
            break;
    }

    return config;
}

} // namespace gui::common
