#pragma once

#include "midi_messages_generated.h"

#include <cstdint>
#include <optional>

namespace gui::esp32
{

struct ParsedMidiMessage {
    midi2pwm::midi::ChannelMessageType type;
    std::uint8_t channel;
    std::uint8_t data1;
    std::uint8_t data2;
};

class MidiByteStreamDecoder
{
public:
    void pushByte(std::uint8_t byte)
    {
        if (messageReady_) {
            return;
        }

        if (isStatusByte(byte)) {
            if (byte >= 0xF0) {
                return;
            }
            runningStatus_ = byte;
            state_ = State::WaitingForData1;
            return;
        }

        if (!isDataByte(byte) || runningStatus_ == 0) {
            return;
        }

        switch (state_) {
        case State::WaitingForStatus:
            break;
        case State::WaitingForData1:
            data1_ = byte;
            if (expectedDataBytes(runningStatus_) == 2) {
                state_ = State::WaitingForData2;
            } else {
                data2_ = 0;
                messageReady_ = true;
                state_ = State::WaitingForData1;
            }
            break;
        case State::WaitingForData2:
            data2_ = byte;
            messageReady_ = true;
            state_ = State::WaitingForData1;
            break;
        }
    }

    std::optional<ParsedMidiMessage> getMessage()
    {
        if (!messageReady_) {
            return std::nullopt;
        }

        const std::uint8_t statusNibble = runningStatus_ & 0xF0;
        const std::uint8_t channel = runningStatus_ & 0x0F;

        midi2pwm::midi::ChannelMessageType msgType;

        switch (statusNibble) {
        case 0x80: msgType = midi2pwm::midi::ChannelMessageType::NoteOff; break;
        case 0x90:
            msgType = (data2_ == 0) ? midi2pwm::midi::ChannelMessageType::NoteOff
                                    : midi2pwm::midi::ChannelMessageType::NoteOn;
            break;
        case 0xA0: msgType = midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure; break;
        case 0xB0: msgType = midi2pwm::midi::ChannelMessageType::ControlChange; break;
        case 0xC0: msgType = midi2pwm::midi::ChannelMessageType::ProgramChange; break;
        case 0xD0: msgType = midi2pwm::midi::ChannelMessageType::ChannelPressure; break;
        case 0xE0: msgType = midi2pwm::midi::ChannelMessageType::PitchBend; break;
        default:
            messageReady_ = false;
            return std::nullopt;
        }

        messageReady_ = false;
        return ParsedMidiMessage{msgType, channel, data1_, data2_};
    }

    void reset()
    {
        state_ = State::WaitingForStatus;
        runningStatus_ = 0;
        data1_ = 0;
        data2_ = 0;
        messageReady_ = false;
    }

private:
    enum class State { WaitingForStatus, WaitingForData1, WaitingForData2 };

    static bool isStatusByte(std::uint8_t byte) { return (byte & 0x80) != 0; }
    static bool isDataByte(std::uint8_t byte) { return (byte & 0x80) == 0; }

    static std::uint8_t expectedDataBytes(std::uint8_t status)
    {
        switch (status & 0xF0) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
        case 0xC0: case 0xD0: return 1;
        default: return 0;
        }
    }

    State state_ = State::WaitingForStatus;
    std::uint8_t runningStatus_ = 0;
    std::uint8_t data1_ = 0;
    std::uint8_t data2_ = 0;
    bool messageReady_ = false;
};

} // namespace gui::esp32
