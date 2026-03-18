#pragma once

#include "midi_messages_generated.h"

#include <atomic>
#include <mutex>

#include "etl/delegate.h"
#include "etl/string.h"

namespace gui::common
{

class MidiMessageViewModel
{
public:
    static constexpr std::size_t kMaxMessageLength = 128U;
    using MessageString = etl::string<kMaxMessageLength>;
    using UpdateCallback = etl::delegate<void(const MessageString &)>;

    void setUpdateCallback(UpdateCallback callback);

    void updateFromChannelMessage(const midi2pwm::midi::ChannelMessageT &message);
    void clear();

    MessageString lastMessage() const;
    MessageString lastTypeLine() const;
    MessageString lastDataLine() const;
    midi2pwm::midi::ChannelMessageT lastRawMessage() const;
    uint32_t version() const { return version_.load(std::memory_order_acquire); }

private:
    static MessageString ToString(const midi2pwm::midi::ChannelMessageT &message);
    static void FormatLines(const midi2pwm::midi::ChannelMessageT &message,
                            MessageString &typeLine, MessageString &dataLine);

    mutable std::mutex mutex_;
    MessageString last_message_{"No midi message received yet"};
    MessageString last_type_line_{"Last MIDI:"};
    MessageString last_data_line_{"---"};
    midi2pwm::midi::ChannelMessageT last_raw_message_{};
    UpdateCallback update_callback_;
    std::atomic<uint32_t> version_{0};
};

} // namespace gui::common
