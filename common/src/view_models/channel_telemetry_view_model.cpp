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
                channelData.instant_data.on_level = instantParams->on_level();
                channelData.instant_data.velocity_sensitive = instantParams->velocity_sensitive();
            }
        } else if (modeParamsType == midi2pwm::pwm::ModeParametersUnion::RampedModeParams) {
            const auto* rampedParams = telemetry.mode_params_as_RampedModeParams();
            if (rampedParams != nullptr) {
                channelData.ramped_data.on_level = rampedParams->on_level();
                channelData.ramped_data.velocity_sensitive = rampedParams->velocity_sensitive();
                channelData.ramped_data.attack_time_ms = rampedParams->attack_time_ms();
                channelData.ramped_data.release_time_ms = rampedParams->release_time_ms();
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
            channelData.instant_data = {255, true};
            channelData.ramped_data = {255, true, 100, 100};
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
