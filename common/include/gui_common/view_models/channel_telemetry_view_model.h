#pragma once

#include "pwm_messages_generated.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "etl/delegate.h"
#include "etl/string.h"

namespace gui::common
{

inline constexpr float deviceToUiPercent(std::uint8_t deviceValue)
{
    return (static_cast<float>(deviceValue) / 255.0F) * 100.0F;
}

inline constexpr std::uint8_t uiPercentToDevice(float uiPercent)
{
    return static_cast<std::uint8_t>((uiPercent / 100.0F) * 255.0F + 0.5F);
}

struct InstantModeData
{
    float on_level;
    bool velocity_sensitive;
};

struct RampedModeData
{
    float on_level;
    bool velocity_sensitive;
    std::uint16_t attack_time_ms;
    std::uint16_t release_time_ms;
};

struct PulseModeData
{
    float on_level;
    bool velocity_sensitive;
    std::uint16_t attack_time_ms;
    std::uint16_t hold_time_ms;
    std::uint16_t release_time_ms;
};

struct ToggleModeData
{
    float on_level;
    bool velocity_sensitive;
    std::uint16_t debounce_delay_ms;
};

struct ADSRModeData
{
    float attack_level;
    float sustain_level;
    bool velocity_sensitive;
    std::uint16_t attack_time_ms;
    std::uint16_t decay_time_ms;
    std::uint16_t release_time_ms;
};

struct CCControlModeData
{
    std::uint8_t cc_number;
    std::uint8_t center_value;
    float left_max_pwm;
    float right_max_pwm;
    std::uint8_t deadband_range;
};

struct PitchBendModeData
{
    float base_level;
    float bend_range;
    bool unipolar;
    bool velocity_sensitive;
};

struct ChannelData
{
    etl::string<16> note;
    float voltage;
    float currentMa;
    float dutyCyclePercent;
    bool isActive;
    etl::string<32> fault;
    std::uint8_t mode_type;
    int polarity;
    int release_action;
    InstantModeData instant_data;
    RampedModeData ramped_data;
    PulseModeData pulse_data;
    ToggleModeData toggle_data;
    ADSRModeData adsr_data;
    CCControlModeData cc_data;
    PitchBendModeData pitchbend_data;
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
    void updateFromConfig(const midi2pwm::pwm::ChannelConfig &config);
    void clear();

    ChannelsArray channels() const;
    uint32_t version() const { return version_.load(std::memory_order_acquire); }

private:
    static etl::string<16> midiNoteToString(std::uint16_t noteNumber);
    static etl::string<32> statusToString(midi2pwm::pwm::ChannelStatus status, bool hadFault);

    mutable std::mutex mutex_;
    ChannelsArray channels_;
    UpdateCallback updateCallback_;
    std::atomic<uint32_t> version_{0};
};

} // namespace gui::common
