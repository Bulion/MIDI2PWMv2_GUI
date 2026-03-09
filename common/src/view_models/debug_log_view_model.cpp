#include "gui_common/view_models/debug_log_view_model.h"

#include <algorithm>
#include <cstring>

namespace gui::common
{

void DebugLogViewModel::setUpdateCallback(UpdateCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    update_callback_ = callback;
}

void DebugLogViewModel::addLogLine(const char *line)
{
    if (!line) {
        return;
    }

    LogLine log_line;
    const std::size_t len = std::strlen(line);
    const std::size_t to_copy = std::min(len, log_line.max_size());
    log_line.append(line, line + to_copy);

    addLogLine(log_line);
}

void DebugLogViewModel::addLogLine(const LogLine &line)
{
    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        log_lines_.push_back(line);

        if (log_lines_.size() > kMaxLogLines) {
            log_lines_.erase(log_lines_.begin());
        }

        callback = update_callback_;
    }

    if (callback.is_valid()) {
        callback();
    }
}

void DebugLogViewModel::clear()
{
    UpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        log_lines_.clear();
        callback = update_callback_;
    }

    if (callback.is_valid()) {
        callback();
    }
}

std::vector<DebugLogViewModel::LogLine> DebugLogViewModel::getLogLines() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return log_lines_;
}

} // namespace gui::common
