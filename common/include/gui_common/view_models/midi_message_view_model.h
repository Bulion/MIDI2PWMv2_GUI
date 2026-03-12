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
    midi2pwm::midi::ChannelMessageT lastRawMessage() const;
    uint32_t version() const { return version_.load(std::memory_order_acquire); }

private:
    static MessageString ToString(const midi2pwm::midi::ChannelMessageT &message);

    mutable std::mutex mutex_;
    MessageString last_message_{"No midi message received yet"};
    midi2pwm::midi::ChannelMessageT last_raw_message_{};
    UpdateCallback update_callback_;
    std::atomic<uint32_t> version_{0};
};

} // namespace gui::common
