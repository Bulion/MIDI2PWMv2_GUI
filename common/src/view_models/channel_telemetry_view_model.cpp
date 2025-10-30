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
        channelData.minValue = telemetry.min_point();
        channelData.maxValue = telemetry.max_point();
        channelData.middleValue = telemetry.midpoint();
        channelData.fault = statusToString(telemetry.status(), telemetry.had_fault());

        GUI_LOG_DEBUG(TAG, "Channel %u: note=%s, voltage=%.1fV, current=%.1fmA, status=%s, config=(%.1f/%.1f/%.1f)",
                      channelNumber,
                      channelData.note.c_str(),
                      channelData.voltage,
                      channelData.current,
                      channelData.fault.c_str(),
                      channelData.minValue,
                      channelData.middleValue,
                      channelData.maxValue);

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
            channelData.minValue = 0.0F;
            channelData.maxValue = 36.0F;
            channelData.middleValue = 18.0F;
            channelData.fault = "";
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
