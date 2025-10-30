#include "gui_common/message_processor.h"
#include "gui_common/log.h"

namespace gui::common
{

MessageProcessor::MessageProcessor()
    : midiEndpoint_(libcomm::MidiEndpoint::WriteCallback::create<&MessageProcessor::NullWrite>(), true)
    , pwmEndpoint_(libcomm::PwmEndpoint::WriteCallback::create<&MessageProcessor::NullWrite>(), true)
{
    midiEndpoint_.OnChannelMessage(
        libcomm::MidiEndpoint::ChannelMessageHandler::create<MessageProcessor, &MessageProcessor::HandleMidiChannelMessage>(*this));

    pwmEndpoint_.OnChannelTelemetry(
        libcomm::PwmEndpoint::ChannelTelemetryHandler::create<MessageProcessor, &MessageProcessor::HandlePwmTelemetry>(*this));

    pwmEndpoint_.OnHeartBeat(
        libcomm::PwmEndpoint::HeartBeatHandler::create<MessageProcessor, &MessageProcessor::HandleHeartBeat>(*this));
}

MessageProcessor::MessageProcessor(WriteCallback writeCallback)
    : midiEndpoint_(writeCallback, true)
    , pwmEndpoint_(writeCallback, true)
{
    midiEndpoint_.OnChannelMessage(
        libcomm::MidiEndpoint::ChannelMessageHandler::create<MessageProcessor, &MessageProcessor::HandleMidiChannelMessage>(*this));

    pwmEndpoint_.OnChannelTelemetry(
        libcomm::PwmEndpoint::ChannelTelemetryHandler::create<MessageProcessor, &MessageProcessor::HandlePwmTelemetry>(*this));

    pwmEndpoint_.OnHeartBeat(
        libcomm::PwmEndpoint::HeartBeatHandler::create<MessageProcessor, &MessageProcessor::HandleHeartBeat>(*this));
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

void MessageProcessor::setHeartBeatCallback(HeartBeatCallback callback)
{
    heartBeatCallback_ = callback;
}

void MessageProcessor::setResponseCallback(ResponseCallback callback)
{
    responseCallback_ = callback;
}

bool MessageProcessor::sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config)
{
    GUI_LOG_INFO("MessageProcessor", "Building ChannelConfig message: ch=%u, note=%u, min=%f, mid=%f, max=%f",
                 config.channel_number, config.note, config.min_point, config.midpoint, config.max_point);

    auto buffer = libcomm::BuildChannelConfigMessage(
        config.channel_number,
        config.configuration,
        config.note,
        config.midpoint,
        config.min_point,
        config.max_point
    );

    GUI_LOG_DEBUG("MessageProcessor", "Buffer size: %zu bytes", buffer.size());

    bool result = pwmEndpoint_.Send(std::move(buffer));

    GUI_LOG_INFO("MessageProcessor", "pwmEndpoint_.Send() returned: %d", result);

    return result;
}

bool MessageProcessor::sendHeartBeat(bool requestTelemetry)
{
    GUI_LOG_DEBUG("MessageProcessor", "Sending HeartBeat message (request_telemetry=%d)", requestTelemetry);

    auto buffer = libcomm::BuildHeartBeatMessage(requestTelemetry);

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
                        GUI_LOG_INFO("MessageProcessor", "Processing MIDI ChannelMessage");
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
                        GUI_LOG_INFO("MessageProcessor", "Processing PWM ChannelTelemetry");
                        HandlePwmTelemetry(*telemetry);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to ChannelTelemetry");
                    }
                } else if (message_type == midi2pwm::pwm::Message::HeartBeat) {
                    auto heartbeat = pwm_envelope->message_as_HeartBeat();
                    if (heartbeat) {
                        GUI_LOG_INFO("MessageProcessor", "Processing PWM HeartBeat");
                        HandleHeartBeat(*heartbeat);
                        return;
                    } else {
                        GUI_LOG_ERROR("MessageProcessor", "Failed to cast to HeartBeat");
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

    if (!has_midi_identifier && !has_pwm_identifier) {
        GUI_LOG_WARNING("MessageProcessor", "Message has neither M2PW nor PWMX file identifier (expected at offset 4)");
    } else {
        GUI_LOG_WARNING("MessageProcessor", "Message could not be parsed as MIDI or PWM envelope");
    }
}

void MessageProcessor::HandleMidiChannelMessage(const midi2pwm::midi::ChannelMessage &message)
{
    GUI_LOG_INFO("MessageProcessor", "MIDI ChannelMessage received: channel=%u, type=%u, data1=%u, data2=%u",
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

void MessageProcessor::HandleHeartBeat(const midi2pwm::pwm::HeartBeat &heartbeat)
{
    GUI_LOG_INFO("MessageProcessor", "HeartBeat received: request_telemetry=%d", heartbeat.request_telemetry());

    if (!heartBeatCallback_.is_valid()) {
        GUI_LOG_DEBUG("MessageProcessor", "HeartBeat callback not registered");
        return;
    }

    heartBeatCallback_(heartbeat);
}

void MessageProcessor::HandleResponse(const midi2pwm::pwm::Response &response)
{
    const char *statusString = (response.status() == midi2pwm::pwm::ResponseStatus::ACK) ? "ACK" : "NACK";

    GUI_LOG_INFO("MessageProcessor", "Response received: status=%s, error_code=%u",
                 statusString, response.error_code());

    if (response.status() == midi2pwm::pwm::ResponseStatus::NACK) {
        GUI_LOG_WARNING("MessageProcessor", "Command was rejected (NACK) with error_code=%u", response.error_code());
    }

    if (!responseCallback_.is_valid()) {
        GUI_LOG_DEBUG("MessageProcessor", "Response callback not registered");
        return;
    }

    responseCallback_(response);
}

} // namespace gui::common
