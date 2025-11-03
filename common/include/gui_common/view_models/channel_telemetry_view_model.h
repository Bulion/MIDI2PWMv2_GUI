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

struct InstantModeData
{
    std::uint8_t on_level;
    bool velocity_sensitive;
};

struct RampedModeData
{
    std::uint8_t on_level;
    bool velocity_sensitive;
    std::uint16_t attack_time_ms;
    std::uint16_t release_time_ms;
};

struct ChannelData
{
    etl::string<16> note;
    float voltage;
    float current;
    etl::string<32> fault;
    std::uint8_t mode_type;
    InstantModeData instant_data;
    RampedModeData ramped_data;
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
