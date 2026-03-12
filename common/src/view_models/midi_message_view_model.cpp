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

    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_message_ = text;
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

} // namespace gui::common
