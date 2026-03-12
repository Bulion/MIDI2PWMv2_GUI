#include "gui_common/message_processor.h"
#include "gui_common/log.h"

#include <inttypes.h>

namespace gui::common
{

MessageProcessor::MessageProcessor()
    : midiEndpoint_(libcomm::MidiEndpoint::WriteCallback::create<&MessageProcessor::NullWrite>())
    , pwmEndpoint_(libcomm::PwmEndpoint::WriteCallback::create<&MessageProcessor::NullWrite>())
    , otaEndpoint_(libcomm::OtaEndpoint::WriteCallback::create<&MessageProcessor::NullWrite>())
{
    midiEndpoint_.OnChannelMessage(
        libcomm::MidiEndpoint::ChannelMessageHandler::create<MessageProcessor, &MessageProcessor::HandleMidiChannelMessage>(*this));

    pwmEndpoint_.OnChannelTelemetry(
        libcomm::PwmEndpoint::ChannelTelemetryHandler::create<MessageProcessor, &MessageProcessor::HandlePwmTelemetry>(*this));

    pwmEndpoint_.OnChannelConfig(
        libcomm::PwmEndpoint::ChannelConfigHandler::create<MessageProcessor, &MessageProcessor::HandleChannelConfig>(*this));

    pwmEndpoint_.OnHeartBeat(
        libcomm::PwmEndpoint::HeartBeatHandler::create<MessageProcessor, &MessageProcessor::HandleHeartBeat>(*this));

    otaEndpoint_.OnProgress(
        libcomm::OtaEndpoint::ProgressHandler::create<MessageProcessor, &MessageProcessor::HandleOtaProgress>(*this));
    otaEndpoint_.OnBegin(
        libcomm::OtaEndpoint::BeginHandler::create<MessageProcessor, &MessageProcessor::HandleOtaBegin>(*this));
    otaEndpoint_.OnData(
        libcomm::OtaEndpoint::DataHandler::create<MessageProcessor, &MessageProcessor::HandleOtaData>(*this));
    otaEndpoint_.OnEnd(
        libcomm::OtaEndpoint::EndHandler::create<MessageProcessor, &MessageProcessor::HandleOtaEnd>(*this));
    otaEndpoint_.OnAbort(
        libcomm::OtaEndpoint::AbortHandler::create<MessageProcessor, &MessageProcessor::HandleOtaAbort>(*this));
}

MessageProcessor::MessageProcessor(WriteCallback writeCallback)
    : midiEndpoint_(writeCallback)
    , pwmEndpoint_(writeCallback)
    , otaEndpoint_(writeCallback)
{
    midiEndpoint_.OnChannelMessage(
        libcomm::MidiEndpoint::ChannelMessageHandler::create<MessageProcessor, &MessageProcessor::HandleMidiChannelMessage>(*this));

    pwmEndpoint_.OnChannelTelemetry(
        libcomm::PwmEndpoint::ChannelTelemetryHandler::create<MessageProcessor, &MessageProcessor::HandlePwmTelemetry>(*this));

    pwmEndpoint_.OnChannelConfig(
        libcomm::PwmEndpoint::ChannelConfigHandler::create<MessageProcessor, &MessageProcessor::HandleChannelConfig>(*this));

    pwmEndpoint_.OnHeartBeat(
        libcomm::PwmEndpoint::HeartBeatHandler::create<MessageProcessor, &MessageProcessor::HandleHeartBeat>(*this));

    otaEndpoint_.OnProgress(
        libcomm::OtaEndpoint::ProgressHandler::create<MessageProcessor, &MessageProcessor::HandleOtaProgress>(*this));
    otaEndpoint_.OnBegin(
        libcomm::OtaEndpoint::BeginHandler::create<MessageProcessor, &MessageProcessor::HandleOtaBegin>(*this));
    otaEndpoint_.OnData(
        libcomm::OtaEndpoint::DataHandler::create<MessageProcessor, &MessageProcessor::HandleOtaData>(*this));
    otaEndpoint_.OnEnd(
        libcomm::OtaEndpoint::EndHandler::create<MessageProcessor, &MessageProcessor::HandleOtaEnd>(*this));
    otaEndpoint_.OnAbort(
        libcomm::OtaEndpoint::AbortHandler::create<MessageProcessor, &MessageProcessor::HandleOtaAbort>(*this));
}

bool MessageProcessor::NullWrite([[maybe_unused]] const std::uint8_t *, [[maybe_unused]] std::size_t)
{
    return false;
}

void MessageProcessor::setMidiChannelMessageCallback(MidiChannelMessageCallback callback)
{
    midiChannelCallback_ = callback;
}

void MessageProcessor::setPwmTelemetryCallback(PwmTelemetryCallback callback)
{
    pwmTelemetryCallback_ = callback;
}

void MessageProcessor::setChannelConfigCallback(ChannelConfigCallback callback)
{
    channelConfigCallback_ = callback;
}

void MessageProcessor::setHeartBeatCallback(HeartBeatCallback callback)
{
    heartBeatCallback_ = callback;
}

void MessageProcessor::setResponseCallback(ResponseCallback callback)
{
    responseCallback_ = callback;
}

void MessageProcessor::setOtaProgressCallback(OtaProgressCallback callback)
{
    otaProgressCallback_ = callback;
}

void MessageProcessor::setOtaBeginCallback(OtaBeginCallback callback)
{
    otaBeginCallback_ = callback;
}

void MessageProcessor::setOtaDataCallback(OtaDataCallback callback)
{
    otaDataCallback_ = callback;
}

void MessageProcessor::setOtaEndCallback(OtaEndCallback callback)
{
    otaEndCallback_ = callback;
}

void MessageProcessor::setOtaAbortCallback(OtaAbortCallback callback)
{
    otaAbortCallback_ = callback;
}

void MessageProcessor::setUnknownFrameCallback(UnknownFrameCallback callback)
{
    unknownFrameCallback_ = callback;
}

bool MessageProcessor::sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config)
{
    GUI_LOG_INFO("MessageProcessor", "Building ChannelConfig message: ch=%u, note=%u, mode=%d",
                 config.channel_number, config.note, static_cast<int>(config.output_mode));

    flatbuffers::FlatBufferBuilder builder;

    auto channel_config = midi2pwm::pwm::ChannelConfig::Pack(builder, &config);

    auto envelope = midi2pwm::pwm::CreateEnvelope(
        builder,
        midi2pwm::pwm::Message::ChannelConfig,
        channel_config.Union()
    );

    builder.Finish(envelope, midi2pwm::pwm::EnvelopeIdentifier());

    flatbuffers::DetachedBuffer buffer = builder.Release();

    bool result = pwmEndpoint_.Send(std::move(buffer));

    GUI_LOG_INFO("MessageProcessor", "pwmEndpoint_.Send() returned: %d", result);

    return result;
}

bool MessageProcessor::sendHeartBeat(bool requestTelemetry, uint16_t epoch)
{
    GUI_LOG_DEBUG("MessageProcessor", "Sending HeartBeat message (request_telemetry=%d, epoch=%u)", requestTelemetry, epoch);

    auto buffer = libcomm::BuildHeartBeatMessage(requestTelemetry, epoch);

    bool result = pwmEndpoint_.Send(std::move(buffer));

    if (!result) {
        GUI_LOG_ERROR("MessageProcessor", "Failed to send HeartBeat - device likely disconnected");
    }

    return result;
}

void MessageProcessor::handleFrame(const std::uint8_t *frame, std::size_t size)
{
    if (!frame || size == 0U) {
        GUI_LOG_WARNING("MessageProcessor", "handleFrame called with null or empty frame");
        return;
    }

    GUI_LOG_DEBUG("MessageProcessor", "handleFrame called with %zu bytes (standard FlatBuffers)", size);

    if (size >= 8) {
        GUI_LOG_DEBUG("MessageProcessor", "First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                      frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7]);
    }

    bool has_midi_identifier = midi2pwm::midi::EnvelopeBufferHasIdentifier(frame);
    bool has_pwm_identifier = midi2pwm::pwm::EnvelopeBufferHasIdentifier(frame);

    GUI_LOG_DEBUG("MessageProcessor", "File identifier check: M2PW=%d, PWMX=%d",
                  has_midi_identifier, has_pwm_identifier);

    if (has_midi_identifier) {
        GUI_LOG_DEBUG("MessageProcessor", "Detected MIDI envelope (M2PW identifier at offset 4)");

        flatbuffers::Verifier verifier(frame, size);
        bool midi_verified = midi2pwm::midi::VerifyEnvelopeBuffer(verifier);

        GUI_LOG_DEBUG("MessageProcessor", "MIDI envelope verification: %s", midi_verified ? "PASS" : "FAIL");

        if (midi_verified) {
            const midi2pwm::midi::Envelope *midi_envelope = midi2pwm::midi::GetEnvelope(frame);

            if (midi_envelope) {
                GUI_LOG_DEBUG("MessageProcessor", "Got MIDI envelope pointer");

                auto packet_type = midi_envelope->packet_type();
                GUI_LOG_DEBUG("MessageProcessor", "MIDI packet type: %u", static_cast<unsigned>(packet_type));

                if (packet_type == midi2pwm::midi::Packet::ChannelMessage) {
                    auto channel_msg = midi_envelope->packet_as_ChannelMessage();
                    if (channel_msg) {
                        GUI_LOG_DEBUG("MessageProcessor", "Processing MIDI ChannelMessage");
                        HandleMidiChannelMessage(*channel_msg);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to ChannelMessage");
                    }
                } else {
                    GUI_LOG_DEBUG("MessageProcessor", "Not a ChannelMessage (type=%u)", static_cast<unsigned>(packet_type));
                }
            } else {
                GUI_LOG_ERROR("MessageProcessor", "Failed to get MIDI envelope pointer");
            }
        }
    }

    if (has_pwm_identifier) {
        GUI_LOG_DEBUG("MessageProcessor", "Detected PWM envelope (PWMX identifier at offset 4)");

        flatbuffers::Verifier verifier(frame, size);
        bool pwm_verified = midi2pwm::pwm::VerifyEnvelopeBuffer(verifier);

        GUI_LOG_DEBUG("MessageProcessor", "PWM envelope verification: %s", pwm_verified ? "PASS" : "FAIL");

        if (pwm_verified) {
            const midi2pwm::pwm::Envelope *pwm_envelope = midi2pwm::pwm::GetEnvelope(frame);

            if (pwm_envelope) {
                GUI_LOG_DEBUG("MessageProcessor", "Got PWM envelope pointer");

                auto message_type = pwm_envelope->message_type();
                GUI_LOG_DEBUG("MessageProcessor", "PWM message type: %u", static_cast<unsigned>(message_type));

                if (message_type == midi2pwm::pwm::Message::ChannelTelemetry) {
                    auto telemetry = pwm_envelope->message_as_ChannelTelemetry();
                    if (telemetry) {
                        GUI_LOG_DEBUG("MessageProcessor", "Processing PWM ChannelTelemetry");
                        HandlePwmTelemetry(*telemetry);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to ChannelTelemetry");
                    }
                } else if (message_type == midi2pwm::pwm::Message::HeartBeat) {
                    auto heartbeat = pwm_envelope->message_as_HeartBeat();
                    if (heartbeat) {
                        GUI_LOG_DEBUG("MessageProcessor", "Processing PWM HeartBeat");
                        HandleHeartBeat(*heartbeat);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to HeartBeat");
                    }
                } else if (message_type == midi2pwm::pwm::Message::ChannelConfig) {
                    auto config = pwm_envelope->message_as_ChannelConfig();
                    if (config) {
                        GUI_LOG_DEBUG("MessageProcessor", "Processing PWM ChannelConfig");
                        HandleChannelConfig(*config);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to ChannelConfig");
                    }
                } else if (message_type == midi2pwm::pwm::Message::Response) {
                    auto response = pwm_envelope->message_as_Response();
                    if (response) {
                        GUI_LOG_INFO("MessageProcessor", "Processing PWM Response");
                        HandleResponse(*response);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to Response");
                    }
                } else {
                    GUI_LOG_DEBUG("MessageProcessor", "Unhandled PWM message type=%u", static_cast<unsigned>(message_type));
                }
            } else {
                GUI_LOG_ERROR("MessageProcessor", "Failed to get PWM envelope pointer");
            }
        }
    }

    bool has_ota_identifier = midi2pwm::ota::EnvelopeBufferHasIdentifier(frame);

    if (has_ota_identifier) {
        flatbuffers::Verifier verifier(frame, size);
        if (!midi2pwm::ota::VerifyEnvelopeBuffer(verifier)) {
            GUI_LOG_WARNING("MessageProcessor", "OTA envelope verification failed");
            return;
        }

        const auto *envelope = midi2pwm::ota::GetEnvelope(frame);
        if (!envelope) {
            return;
        }

        switch (envelope->message_type()) {
        case midi2pwm::ota::Message::OtaProgress:
            if (auto *msg = envelope->message_as_OtaProgress()) {
                HandleOtaProgress(*msg);
                return;
            }
            break;
        case midi2pwm::ota::Message::OtaBegin:
            if (auto *msg = envelope->message_as_OtaBegin()) {
                HandleOtaBegin(*msg);
                return;
            }
            break;
        case midi2pwm::ota::Message::OtaData:
            if (auto *msg = envelope->message_as_OtaData()) {
                HandleOtaData(*msg);
                return;
            }
            break;
        case midi2pwm::ota::Message::OtaEnd:
            if (auto *msg = envelope->message_as_OtaEnd()) {
                HandleOtaEnd(*msg);
                return;
            }
            break;
        case midi2pwm::ota::Message::OtaAbort:
            if (auto *msg = envelope->message_as_OtaAbort()) {
                HandleOtaAbort(*msg);
                return;
            }
            break;
        default:
            break;
        }
    }

    if (!has_midi_identifier && !has_pwm_identifier && !has_ota_identifier) {
        if (unknownFrameCallback_.is_valid()) {
            unknownFrameCallback_(frame, size);
            return;
        }
        GUI_LOG_WARNING("MessageProcessor", "Message has no recognized file identifier (expected M2PW, PWMX, or OTAX)");
    } else {
        GUI_LOG_WARNING("MessageProcessor", "Message could not be parsed");
    }
}

void MessageProcessor::HandleMidiChannelMessage(const midi2pwm::midi::ChannelMessage &message)
{
    GUI_LOG_DEBUG("MessageProcessor", "MIDI ChannelMessage received: channel=%u, type=%u, data1=%u, data2=%u",
                 message.channel(), static_cast<unsigned>(message.message_type()),
                 message.data1(), message.data2());

    if (!midiChannelCallback_.is_valid()) {
        GUI_LOG_ERROR("MessageProcessor", "MIDI channel callback not registered!");
        return;
    }

    midi2pwm::midi::ChannelMessageT native;
    message.UnPackTo(&native);

    GUI_LOG_DEBUG("MessageProcessor", "Unpacked to native type, invoking callback");
    midiChannelCallback_(native);
    GUI_LOG_DEBUG("MessageProcessor", "MIDI callback completed");
}

void MessageProcessor::HandlePwmTelemetry(const midi2pwm::pwm::ChannelTelemetry &telemetry)
{
    GUI_LOG_DEBUG("MessageProcessor", "PWM ChannelTelemetry received: channel=%u, voltage=%f, current=%f",
                  telemetry.channel_number(), telemetry.voltage(), telemetry.current());

    if (!pwmTelemetryCallback_.is_valid()) {
        GUI_LOG_WARNING("MessageProcessor", "PWM telemetry callback not registered");
        return;
    }

    pwmTelemetryCallback_(telemetry);
}

void MessageProcessor::HandleChannelConfig(const midi2pwm::pwm::ChannelConfig &config)
{
    GUI_LOG_DEBUG("MessageProcessor", "ChannelConfig received: channel=%u, mode=%u",
                  config.channel_number(), static_cast<unsigned>(config.output_mode()));

    if (!channelConfigCallback_.is_valid()) {
        GUI_LOG_WARNING("MessageProcessor", "ChannelConfig callback not registered");
        return;
    }

    channelConfigCallback_(config);
}

void MessageProcessor::HandleHeartBeat(const midi2pwm::pwm::HeartBeat &heartbeat)
{
    GUI_LOG_DEBUG("MessageProcessor", "HeartBeat received: request_telemetry=%d", heartbeat.request_telemetry());

    if (!heartBeatCallback_.is_valid()) {
        GUI_LOG_DEBUG("MessageProcessor", "HeartBeat callback not registered");
        return;
    }

    heartBeatCallback_(heartbeat);
}

void MessageProcessor::HandleResponse(const midi2pwm::pwm::Response &response)
{
    const char *statusString = (response.status() == midi2pwm::pwm::ResponseStatus::ACK) ? "ACK" : "NACK";

    GUI_LOG_INFO("MessageProcessor", "Response received: status=%s, error_code=%" PRIu32,
                 statusString, response.error_code());

    if (response.status() == midi2pwm::pwm::ResponseStatus::NACK) {
        GUI_LOG_WARNING("MessageProcessor", "Command was rejected (NACK) with error_code=%" PRIu32, response.error_code());
    }

    if (!responseCallback_.is_valid()) {
        GUI_LOG_DEBUG("MessageProcessor", "Response callback not registered");
        return;
    }

    responseCallback_(response);
}

void MessageProcessor::HandleOtaProgress(const midi2pwm::ota::OtaProgress &progress)
{
    GUI_LOG_INFO("MessageProcessor", "OtaProgress received: status=%u, chunks=%u/%u",
                 static_cast<unsigned>(progress.status()),
                 progress.chunks_received(), progress.total_chunks());

    if (otaProgressCallback_.is_valid()) {
        otaProgressCallback_(progress);
    }
}

void MessageProcessor::HandleOtaBegin(const midi2pwm::ota::OtaBegin &begin)
{
    GUI_LOG_INFO("MessageProcessor", "OtaBegin received: target=%u, size=%lu",
                 static_cast<unsigned>(begin.target()),
                 static_cast<unsigned long>(begin.firmware_size()));

    if (otaBeginCallback_.is_valid()) {
        otaBeginCallback_(begin);
    }
}

void MessageProcessor::HandleOtaData(const midi2pwm::ota::OtaData &data)
{
    if (otaDataCallback_.is_valid()) {
        otaDataCallback_(data);
    }
}

void MessageProcessor::HandleOtaEnd(const midi2pwm::ota::OtaEnd &end)
{
    GUI_LOG_INFO("MessageProcessor", "OtaEnd received: target=%u",
                 static_cast<unsigned>(end.target()));

    if (otaEndCallback_.is_valid()) {
        otaEndCallback_(end);
    }
}

void MessageProcessor::HandleOtaAbort(const midi2pwm::ota::OtaAbort &abort)
{
    GUI_LOG_INFO("MessageProcessor", "OtaAbort received: target=%u",
                 static_cast<unsigned>(abort.target()));

    if (otaAbortCallback_.is_valid()) {
        otaAbortCallback_(abort);
    }
}

bool MessageProcessor::sendOtaBegin(midi2pwm::ota::Target target, std::uint32_t firmwareSize,
                                     std::uint32_t firmwareCrc32, const char *versionString,
                                     std::uint16_t totalChunks)
{
    auto buffer = libcomm::BuildOtaBeginMessage(target, firmwareSize, firmwareCrc32, versionString, totalChunks);
    return otaEndpoint_.Send(std::move(buffer));
}

bool MessageProcessor::sendOtaData(midi2pwm::ota::Target target, std::uint16_t chunkIndex,
                                    const std::uint8_t *data, std::size_t dataSize)
{
    auto buffer = libcomm::BuildOtaDataMessage(target, chunkIndex, data, dataSize);
    return otaEndpoint_.Send(std::move(buffer));
}

bool MessageProcessor::sendOtaEnd(midi2pwm::ota::Target target)
{
    auto buffer = libcomm::BuildOtaEndMessage(target);
    return otaEndpoint_.Send(std::move(buffer));
}

bool MessageProcessor::sendOtaAbort(midi2pwm::ota::Target target, const char *reason)
{
    auto buffer = libcomm::BuildOtaAbortMessage(target, reason);
    return otaEndpoint_.Send(std::move(buffer));
}

bool MessageProcessor::sendOtaProgress(midi2pwm::ota::Target target, midi2pwm::ota::OtaStatus status,
                                        std::uint16_t chunksReceived, std::uint16_t totalChunks,
                                        const char *errorMessage)
{
    auto buffer = libcomm::BuildOtaProgressMessage(target, status, chunksReceived, totalChunks, errorMessage);
    return otaEndpoint_.Send(std::move(buffer));
}

} // namespace gui::common
