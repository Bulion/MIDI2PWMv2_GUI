#pragma once

#include "libcomm/midi_endpoint.h"
#include "libcomm/pwm_endpoint.h"
#include "midi_messages_generated.h"
#include "pwm_messages_generated.h"

#include <cstddef>
#include <cstdint>

#include "etl/delegate.h"

namespace gui::common
{

class MessageProcessor
{
public:
    using MidiChannelMessageCallback = etl::delegate<void(const midi2pwm::midi::ChannelMessageT &)>;
    using PwmTelemetryCallback = etl::delegate<void(const midi2pwm::pwm::ChannelTelemetry &)>;
    using ChannelConfigCallback = etl::delegate<void(const midi2pwm::pwm::ChannelConfig &)>;
    using HeartBeatCallback = etl::delegate<void(const midi2pwm::pwm::HeartBeat &)>;
    using ResponseCallback = etl::delegate<void(const midi2pwm::pwm::Response &)>;
    using WriteCallback = etl::delegate<bool(const std::uint8_t *, std::size_t)>;

    MessageProcessor();
    explicit MessageProcessor(WriteCallback writeCallback);

    void setMidiChannelMessageCallback(MidiChannelMessageCallback callback);
    void setPwmTelemetryCallback(PwmTelemetryCallback callback);
    void setChannelConfigCallback(ChannelConfigCallback callback);
    void setHeartBeatCallback(HeartBeatCallback callback);
    void setResponseCallback(ResponseCallback callback);
    void handleFrame(const std::uint8_t *frame, std::size_t size);

    bool sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config);
    bool sendHeartBeat(bool requestTelemetry, uint16_t epoch);

private:
    static bool NullWrite(const std::uint8_t *, std::size_t);
    void HandleMidiChannelMessage(const midi2pwm::midi::ChannelMessage &message);
    void HandlePwmTelemetry(const midi2pwm::pwm::ChannelTelemetry &telemetry);
    void HandleChannelConfig(const midi2pwm::pwm::ChannelConfig &config);
    void HandleHeartBeat(const midi2pwm::pwm::HeartBeat &heartbeat);
    void HandleResponse(const midi2pwm::pwm::Response &response);

    libcomm::MidiEndpoint midiEndpoint_;
    libcomm::PwmEndpoint pwmEndpoint_;
    MidiChannelMessageCallback midiChannelCallback_;
    PwmTelemetryCallback pwmTelemetryCallback_;
    ChannelConfigCallback channelConfigCallback_;
    HeartBeatCallback heartBeatCallback_;
    ResponseCallback responseCallback_;
};

} // namespace gui::common
