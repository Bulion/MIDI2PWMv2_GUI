#pragma once

#include "pwm_messages_generated.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

#include "etl/delegate.h"
#include "etl/string.h"

namespace gui::common
{

struct ChannelData
{
    etl::string<16> note;
    float voltage;
    float current;
    float minValue;
    float maxValue;
    float middleValue;
    etl::string<32> fault;
};

class ChannelTelemetryViewModel
{
public:
    static constexpr std::size_t CHANNEL_COUNT = 16;
    using ChannelsArray = std::array<ChannelData, CHANNEL_COUNT>;
    using UpdateCallback = etl::delegate<void(const ChannelsArray &)>;

    ChannelTelemetryViewModel();

    void setUpdateCallback(UpdateCallback callback);
    void updateFromTelemetry(const midi2pwm::pwm::ChannelTelemetry &telemetry);
    void clear();

    ChannelsArray channels() const;

private:
    static etl::string<16> midiNoteToString(std::uint16_t noteNumber);
    static etl::string<32> statusToString(midi2pwm::pwm::ChannelStatus status, bool hadFault);

    mutable std::mutex mutex_;
    ChannelsArray channels_;
    UpdateCallback updateCallback_;
};

} // namespace gui::common
