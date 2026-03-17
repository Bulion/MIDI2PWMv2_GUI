#pragma once

#include "gui_common/view_models/connection_view_model.h"

#include <atomic>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace gui::common
{

class CommLoop
{
public:
    struct ConnectCmd
    {
        std::string portPath;
    };

    struct DisconnectCmd
    {
    };

    struct SendConfigCmd
    {
        midi2pwm::pwm::ChannelConfigT config;
    };

    struct ResetFaultCmd
    {
        std::uint16_t channelNumber;
    };

    struct RefreshPortsCmd
    {
    };

    using Command = std::variant<ConnectCmd, DisconnectCmd, SendConfigCmd, ResetFaultCmd, RefreshPortsCmd>;

    explicit CommLoop(ConnectionViewModel &vm);

    void post(Command cmd);
    void runOnce();
    void requestStop();
    bool shouldStop() const;

private:
    ConnectionViewModel &vm_;
    std::mutex cmdMutex_;
    std::vector<Command> pending_;
    std::atomic<bool> stopRequested_{false};
};

} // namespace gui::common
