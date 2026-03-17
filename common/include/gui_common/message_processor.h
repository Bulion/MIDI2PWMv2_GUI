#pragma once

#include "libcomm/log_endpoint.h"
#include "libcomm/midi_endpoint.h"
#include "libcomm/ota_endpoint.h"
#include "libcomm/pwm_endpoint.h"
#include "midi_messages_generated.h"
#include "ota_messages_generated.h"
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
    using MidiForwardCallback = etl::delegate<void(const midi2pwm::midi::ChannelMessageT &)>;
    using BatchTelemetryCallback = etl::delegate<void(const midi2pwm::pwm::BatchTelemetry &)>;
    using ChannelConfigCallback = etl::delegate<void(const midi2pwm::pwm::ChannelConfig &)>;
    using BatchConfigCallback = etl::delegate<void(const midi2pwm::pwm::BatchConfig &)>;
    using HeartBeatCallback = etl::delegate<void(const midi2pwm::pwm::HeartBeat &)>;
    using ResponseCallback = etl::delegate<void(const midi2pwm::pwm::Response &)>;
    using OtaProgressCallback = etl::delegate<void(const midi2pwm::ota::OtaProgress &)>;
    using OtaBeginCallback = etl::delegate<void(const midi2pwm::ota::OtaBegin &)>;
    using OtaDataCallback = etl::delegate<void(const midi2pwm::ota::OtaData &)>;
    using OtaEndCallback = etl::delegate<void(const midi2pwm::ota::OtaEnd &)>;
    using OtaAbortCallback = etl::delegate<void(const midi2pwm::ota::OtaAbort &)>;
    using UnknownFrameCallback = etl::delegate<bool(const std::uint8_t *, std::size_t)>;
    using WriteCallback = etl::delegate<bool(const std::uint8_t *, std::size_t)>;

    MessageProcessor();
    explicit MessageProcessor(WriteCallback writeCallback);

    void setMidiChannelMessageCallback(MidiChannelMessageCallback callback);
    void setBatchTelemetryCallback(BatchTelemetryCallback callback);
    void setChannelConfigCallback(ChannelConfigCallback callback);
    void setBatchConfigCallback(BatchConfigCallback callback);
    void setHeartBeatCallback(HeartBeatCallback callback);
    void setResponseCallback(ResponseCallback callback);
    void setOtaProgressCallback(OtaProgressCallback callback);
    void setOtaBeginCallback(OtaBeginCallback callback);
    void setOtaDataCallback(OtaDataCallback callback);
    void setOtaEndCallback(OtaEndCallback callback);
    void setOtaAbortCallback(OtaAbortCallback callback);
    void setMidiForwardCallback(MidiForwardCallback callback);
    void setUnknownFrameCallback(UnknownFrameCallback callback);
    void handleFrame(const std::uint8_t *frame, std::size_t size);

    bool sendMidiChannelMessage(midi2pwm::midi::ChannelMessageType type,
                                std::uint8_t channel, std::uint8_t data1, std::uint8_t data2);
    bool sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config);
    bool sendHeartBeat(bool requestTelemetry, uint16_t epoch);
    bool sendOtaBegin(midi2pwm::ota::Target target, std::uint32_t firmwareSize,
                      std::uint32_t firmwareCrc32, const char *versionString,
                      std::uint16_t totalChunks);
    bool sendOtaData(midi2pwm::ota::Target target, std::uint16_t chunkIndex,
                     const std::uint8_t *data, std::size_t dataSize);
    bool sendOtaEnd(midi2pwm::ota::Target target);
    bool sendOtaAbort(midi2pwm::ota::Target target, const char *reason);
    bool sendOtaProgress(midi2pwm::ota::Target target, midi2pwm::ota::OtaStatus status,
                         std::uint16_t chunksReceived, std::uint16_t totalChunks,
                         const char *errorMessage = nullptr);
    bool sendLogForward(midi2pwm::log::LogLevel level, const char *tag, const char *message);

private:
    static bool NullWrite(const std::uint8_t *, std::size_t);
    void HandleMidiChannelMessage(const midi2pwm::midi::ChannelMessage &message);
    void HandleBatchTelemetry(const midi2pwm::pwm::BatchTelemetry &batch);
    void HandleChannelConfig(const midi2pwm::pwm::ChannelConfig &config);
    void HandleBatchConfig(const midi2pwm::pwm::BatchConfig &batch);
    void HandleHeartBeat(const midi2pwm::pwm::HeartBeat &heartbeat);
    void HandleResponse(const midi2pwm::pwm::Response &response);
    void HandleOtaProgress(const midi2pwm::ota::OtaProgress &progress);
    void HandleOtaBegin(const midi2pwm::ota::OtaBegin &begin);
    void HandleOtaData(const midi2pwm::ota::OtaData &data);
    void HandleOtaEnd(const midi2pwm::ota::OtaEnd &end);
    void HandleOtaAbort(const midi2pwm::ota::OtaAbort &abort);

    libcomm::LogEndpoint logEndpoint_;
    libcomm::MidiEndpoint midiEndpoint_;
    libcomm::PwmEndpoint pwmEndpoint_;
    libcomm::OtaEndpoint otaEndpoint_;
    MidiChannelMessageCallback midiChannelCallback_;
    MidiForwardCallback midiForwardCallback_;
    BatchTelemetryCallback batchTelemetryCallback_;
    ChannelConfigCallback channelConfigCallback_;
    BatchConfigCallback batchConfigCallback_;
    HeartBeatCallback heartBeatCallback_;
    ResponseCallback responseCallback_;
    OtaProgressCallback otaProgressCallback_;
    OtaBeginCallback otaBeginCallback_;
    OtaDataCallback otaDataCallback_;
    OtaEndCallback otaEndCallback_;
    OtaAbortCallback otaAbortCallback_;
    UnknownFrameCallback unknownFrameCallback_;
};

} // namespace gui::common
