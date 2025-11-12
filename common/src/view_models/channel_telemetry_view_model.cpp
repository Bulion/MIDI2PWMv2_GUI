#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/log.h"

#include <cstdio>

namespace gui::common
{

static constexpr const char* TAG = "ChannelTelemetry";

ChannelTelemetryViewModel::ChannelTelemetryViewModel()
{
    clear();
}

void ChannelTelemetryViewModel::setUpdateCallback(UpdateCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    updateCallback_ = callback;
}

void ChannelTelemetryViewModel::updateFromTelemetry(const midi2pwm::pwm::ChannelTelemetry &telemetry)
{
    const std::uint16_t channelNumber = telemetry.channel_number();

    if (channelNumber >= CHANNEL_COUNT) {
        GUI_LOG_ERROR(TAG, "Invalid channel number: %u", channelNumber);
        return;
    }

    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        ChannelData &channelData = channels_[channelNumber];
        channelData.note = midiNoteToString(telemetry.note());
        channelData.voltage = telemetry.voltage();
        channelData.current = telemetry.current();
        channelData.fault = statusToString(telemetry.status(), telemetry.had_fault());

        channelData.mode_type = static_cast<std::uint8_t>(telemetry.output_mode());

        const auto modeParamsType = telemetry.mode_params_type();
        if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::InstantModeParams) {
            const auto* instantParams = telemetry.mode_params_as_InstantModeParams();
            if (instantParams != nullptr) {
                channelData.instant_data.on_level = deviceToUiPercent(instantParams->on_level());
                channelData.instant_data.velocity_sensitive = instantParams->velocity_sensitive();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::RampedModeParams) {
            const auto* rampedParams = telemetry.mode_params_as_RampedModeParams();
            if (rampedParams != nullptr) {
                channelData.ramped_data.on_level = deviceToUiPercent(rampedParams->on_level());
                channelData.ramped_data.velocity_sensitive = rampedParams->velocity_sensitive();
                channelData.ramped_data.attack_time_ms = rampedParams->attack_time_ms();
                channelData.ramped_data.release_time_ms = rampedParams->release_time_ms();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::PulseModeParams) {
            const auto* pulseParams = telemetry.mode_params_as_PulseModeParams();
            if (pulseParams != nullptr) {
                channelData.pulse_data.on_level = deviceToUiPercent(pulseParams->on_level());
                channelData.pulse_data.velocity_sensitive = pulseParams->velocity_sensitive();
                channelData.pulse_data.attack_time_ms = pulseParams->attack_time_ms();
                channelData.pulse_data.hold_time_ms = pulseParams->hold_time_ms();
                channelData.pulse_data.release_time_ms = pulseParams->release_time_ms();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::ToggleModeParams) {
            const auto* toggleParams = telemetry.mode_params_as_ToggleModeParams();
            if (toggleParams != nullptr) {
                channelData.toggle_data.on_level = deviceToUiPercent(toggleParams->on_level());
                channelData.toggle_data.velocity_sensitive = toggleParams->velocity_sensitive();
                channelData.toggle_data.debounce_delay_ms = toggleParams->debounce_delay_ms();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::ADSRModeParams) {
            const auto* adsrParams = telemetry.mode_params_as_ADSRModeParams();
            if (adsrParams != nullptr) {
                channelData.adsr_data.attack_level = deviceToUiPercent(adsrParams->attack_level());
                channelData.adsr_data.sustain_level = deviceToUiPercent(adsrParams->sustain_level());
                channelData.adsr_data.velocity_sensitive = adsrParams->velocity_sensitive();
                channelData.adsr_data.attack_time_ms = adsrParams->attack_time_ms();
                channelData.adsr_data.decay_time_ms = adsrParams->decay_time_ms();
                channelData.adsr_data.release_time_ms = adsrParams->release_time_ms();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::CCControlModeParams) {
            const auto* ccParams = telemetry.mode_params_as_CCControlModeParams();
            if (ccParams != nullptr) {
                channelData.cc_data.cc_number = ccParams->cc_number();
                channelData.cc_data.center_value = ccParams->center_value();
                channelData.cc_data.left_max_pwm = deviceToUiPercent(ccParams->left_max_pwm());
                channelData.cc_data.right_max_pwm = deviceToUiPercent(ccParams->right_max_pwm());
                channelData.cc_data.deadband_range = ccParams->deadband_range();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::PitchBendModeParams) {
            const auto* pitchbendParams = telemetry.mode_params_as_PitchBendModeParams();
            if (pitchbendParams != nullptr) {
                channelData.pitchbend_data.base_level = deviceToUiPercent(pitchbendParams->base_level());
                channelData.pitchbend_data.bend_range = deviceToUiPercent(pitchbendParams->bend_range());
                channelData.pitchbend_data.unipolar = pitchbendParams->unipolar();
                channelData.pitchbend_data.velocity_sensitive = pitchbendParams->velocity_sensitive();
            }
        }

        GUI_LOG_DEBUG(TAG, "Channel %u: note=%s, voltage=%.1fV, current=%.1fmA, status=%s",
                      channelNumber,
                      channelData.note.c_str(),
                      channelData.voltage,
                      channelData.current,
                      channelData.fault.c_str());

        callback = updateCallback_;
    }

    if (callback.is_valid()) {
        callback(channels_);
    }
}

void ChannelTelemetryViewModel::clear()
{
    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto &channelData : channels_) {
            channelData.note = "---";
            channelData.voltage = 0.0F;
            channelData.current = 0.0F;
            channelData.fault = "";
            channelData.mode_type = 0;
            channelData.instant_data = {100.0F, true};
            channelData.ramped_data = {100.0F, true, 100, 100};
            channelData.pulse_data = {100.0F, true, 10, 100, 50};
            channelData.toggle_data = {100.0F, false, 50};
            channelData.adsr_data = {100.0F, deviceToUiPercent(200), true, 50, 100, 200};
            channelData.cc_data = {1, 64, 100.0F, 100.0F, 2};
            channelData.pitchbend_data = {deviceToUiPercent(128), deviceToUiPercent(64), false, true};
        }

        callback = updateCallback_;
    }

    if (callback.is_valid()) {
        callback(channels_);
    }
}

ChannelTelemetryViewModel::ChannelsArray ChannelTelemetryViewModel::channels() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_;
}

etl::string<16> ChannelTelemetryViewModel::midiNoteToString(std::uint16_t noteNumber)
{
    static constexpr const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    if (noteNumber > 127U) {
        return "---";
    }

    const int octave = static_cast<int>(noteNumber / 12U) - 1;
    const std::size_t semitone = noteNumber % 12U;

    etl::string<16> result;
    result.append(NOTE_NAMES[semitone]);

    char octaveBuffer[8];
    const int written = std::snprintf(octaveBuffer, sizeof(octaveBuffer), "%d", octave);
    if (written > 0) {
        octaveBuffer[sizeof(octaveBuffer) - 1] = '\0';
        result.append(octaveBuffer);
    }

    return result;
}

etl::string<32> ChannelTelemetryViewModel::statusToString(midi2pwm::pwm::ChannelStatus status, bool hadFault)
{
    if (status == midi2pwm::pwm::ChannelStatus::Fault) {
        return "FAULT";
    }

    if (hadFault) {
        return "HAD_FAULT";
    }

    switch (status) {
    case midi2pwm::pwm::ChannelStatus::Active:
        return "Active";
    case midi2pwm::pwm::ChannelStatus::Inactive:
        return "Inactive";
    default:
        return "";
    }
}

} // namespace gui::common
