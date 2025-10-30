#pragma once

#include "midi_messages_generated.h"

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

private:
    static MessageString ToString(const midi2pwm::midi::ChannelMessageT &message);

    mutable std::mutex mutex_;
    MessageString last_message_{"No midi message received yet"};
    UpdateCallback update_callback_;
};

} // namespace gui::common
