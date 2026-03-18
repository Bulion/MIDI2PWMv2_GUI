#include "gui_common/view_models/midi_message_view_model.h"
#include "gui_common/log.h"

#include <cstdio>
#include <cstring>

namespace gui::common
{

static constexpr const char* TAG = "MidiMessage";

void MidiMessageViewModel::setUpdateCallback(UpdateCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    update_callback_ = callback;
}

void MidiMessageViewModel::updateFromChannelMessage(const midi2pwm::midi::ChannelMessageT &message)
{
    GUI_LOG_INFO(TAG, "updateFromChannelMessage called: channel=%u, type=%u, data1=%u, data2=%u",
                 message.channel, static_cast<unsigned>(message.message_type), message.data1, message.data2);

    const MessageString text = ToString(message);

    GUI_LOG_INFO(TAG, "Formatted message: %s", text.c_str());

    MessageString typeLine;
    MessageString dataLine;
    FormatLines(message, typeLine, dataLine);

    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_message_ = text;
        last_type_line_ = typeLine;
        last_data_line_ = dataLine;
        last_raw_message_ = message;
        callback = update_callback_;
    }

    version_.fetch_add(1, std::memory_order_release);

    if (callback.is_valid()) {
        GUI_LOG_DEBUG(TAG, "Invoking update callback");
        callback(last_message_);
        GUI_LOG_DEBUG(TAG, "Update callback completed");
    } else {
        GUI_LOG_ERROR(TAG, "Update callback not registered!");
    }
}

void MidiMessageViewModel::clear()
{
    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_message_ = "No midi message received yet";
        last_type_line_ = "Last MIDI:";
        last_data_line_ = "---";
        last_raw_message_ = {};
        callback = update_callback_;
    }

    version_.fetch_add(1, std::memory_order_release);

    if (callback.is_valid()) {
        callback(last_message_);
    }
}

MidiMessageViewModel::MessageString MidiMessageViewModel::lastMessage() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_message_;
}

MidiMessageViewModel::MessageString MidiMessageViewModel::lastTypeLine() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_type_line_;
}

MidiMessageViewModel::MessageString MidiMessageViewModel::lastDataLine() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_data_line_;
}

midi2pwm::midi::ChannelMessageT MidiMessageViewModel::lastRawMessage() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_raw_message_;
}

MidiMessageViewModel::MessageString MidiMessageViewModel::ToString(const midi2pwm::midi::ChannelMessageT &message)
{
    MessageString msg;

    const auto append_cstr = [&msg](const char *str) {
        if (!str) {
            return;
        }
        const std::size_t remaining = msg.max_size() - msg.size();
        if (remaining == 0U) {
            return;
        }
        const std::size_t len = std::strlen(str);
        const std::size_t to_copy = (len < remaining) ? len : remaining;
        msg.append(str, str + to_copy);
    };

    const auto append_format = [&msg, &append_cstr](const char *fmt, auto... args) {
        char buffer[64];
        const int written = std::snprintf(buffer, sizeof(buffer), fmt, args...);
        if (written > 0) {
            buffer[sizeof(buffer) - 1] = '\0';
            append_cstr(buffer);
        }
    };

    append_format("Channel %d: ", static_cast<int>(message.channel));

    switch (message.message_type) {
    case midi2pwm::midi::ChannelMessageType::NoteOn:
        append_format("NoteOn note=%u velocity=%u", message.data1, message.data2);
        break;
    case midi2pwm::midi::ChannelMessageType::NoteOff:
        append_format("NoteOff note=%u velocity=%u", message.data1, message.data2);
        break;
    case midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure:
        append_format("PolyPressure note=%u value=%u", message.data1, message.data2);
        break;
    case midi2pwm::midi::ChannelMessageType::ControlChange:
        append_format("ControlChange cc=%u value=%u", message.data1, message.data2);
        break;
    case midi2pwm::midi::ChannelMessageType::ProgramChange:
        append_format("ProgramChange program=%u", message.data1);
        break;
    case midi2pwm::midi::ChannelMessageType::ChannelPressure:
        append_format("ChannelPressure value=%u", message.data1);
        break;
    case midi2pwm::midi::ChannelMessageType::PitchBend:
        append_format("PitchBend value=%u", message.data1);
        break;
    default:
        append_cstr("Unknown message");
        break;
    }

    return msg;
}

static const char* noteNameFromMidi(uint8_t note)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[note % 12];
}

static int octaveFromMidi(uint8_t note)
{
    return static_cast<int>(note / 12) - 1;
}

void MidiMessageViewModel::FormatLines(const midi2pwm::midi::ChannelMessageT &message,
                                       MessageString &typeLine, MessageString &dataLine)
{
    char buf[64];

    switch (message.message_type) {
    case midi2pwm::midi::ChannelMessageType::NoteOn:
        std::snprintf(buf, sizeof(buf), "Ch %u  NoteOn  Vel %u", message.channel + 1, message.data2);
        typeLine = buf;
        std::snprintf(buf, sizeof(buf), "%s%d (%u)", noteNameFromMidi(message.data1), octaveFromMidi(message.data1), message.data1);
        dataLine = buf;
        break;
    case midi2pwm::midi::ChannelMessageType::NoteOff:
        std::snprintf(buf, sizeof(buf), "Ch %u  NoteOff", message.channel + 1);
        typeLine = buf;
        std::snprintf(buf, sizeof(buf), "%s%d (%u)", noteNameFromMidi(message.data1), octaveFromMidi(message.data1), message.data1);
        dataLine = buf;
        break;
    case midi2pwm::midi::ChannelMessageType::ControlChange:
        std::snprintf(buf, sizeof(buf), "Ch %u  CC", message.channel + 1);
        typeLine = buf;
        std::snprintf(buf, sizeof(buf), "CC %u  Val %u", message.data1, message.data2);
        dataLine = buf;
        break;
    case midi2pwm::midi::ChannelMessageType::PitchBend:
        std::snprintf(buf, sizeof(buf), "Ch %u  PitchBend", message.channel + 1);
        typeLine = buf;
        std::snprintf(buf, sizeof(buf), "Value %u", static_cast<unsigned>((message.data2 << 7) | message.data1));
        dataLine = buf;
        break;
    case midi2pwm::midi::ChannelMessageType::ProgramChange:
        std::snprintf(buf, sizeof(buf), "Ch %u  Program", message.channel + 1);
        typeLine = buf;
        std::snprintf(buf, sizeof(buf), "Program %u", message.data1);
        dataLine = buf;
        break;
    default:
        std::snprintf(buf, sizeof(buf), "Ch %u  MIDI", message.channel + 1);
        typeLine = buf;
        std::snprintf(buf, sizeof(buf), "D1=%u D2=%u", message.data1, message.data2);
        dataLine = buf;
        break;
    }
}

} // namespace gui::common
