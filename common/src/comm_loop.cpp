#include "gui_common/comm_loop.h"
#include "gui_common/log.h"

namespace gui::common
{

static constexpr const char *TAG = "CommLoop";

CommLoop::CommLoop(ConnectionViewModel &vm)
    : vm_(vm)
{
}

void CommLoop::post(Command cmd)
{
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pending_.push_back(std::move(cmd));
}

void CommLoop::runOnce()
{
    std::vector<Command> commands;
    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        commands.swap(pending_);
    }

    for (auto &cmd : commands) {
        std::visit([this](auto &c) {
            using T = std::decay_t<decltype(c)>;

            if constexpr (std::is_same_v<T, ConnectCmd>) {
                GUI_LOG_INFO(TAG, "Connecting to %s", c.portPath.c_str());
                if (!vm_.connect(c.portPath)) {
                    GUI_LOG_ERROR(TAG, "Failed to connect to %s", c.portPath.c_str());
                }
            } else if constexpr (std::is_same_v<T, DisconnectCmd>) {
                GUI_LOG_INFO(TAG, "Disconnecting");
                vm_.disconnect();
            } else if constexpr (std::is_same_v<T, SendConfigCmd>) {
                if (!vm_.sendChannelConfig(c.config)) {
                    GUI_LOG_ERROR(TAG, "Failed to send channel config");
                }
            } else if constexpr (std::is_same_v<T, ResetFaultCmd>) {
                if (!vm_.sendFaultReset(c.channelNumber)) {
                    GUI_LOG_ERROR(TAG, "Failed to send fault reset for channel %u", c.channelNumber);
                }
            } else if constexpr (std::is_same_v<T, RefreshPortsCmd>) {
                vm_.refreshPorts();
            }
        }, cmd);
    }

    vm_.update();
}

void CommLoop::requestStop()
{
    stopRequested_.store(true, std::memory_order_release);
}

bool CommLoop::shouldStop() const
{
    return stopRequested_.load(std::memory_order_acquire);
}

} // namespace gui::common
