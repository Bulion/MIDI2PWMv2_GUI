#pragma once

#include <mutex>
#include <vector>

#include "etl/delegate.h"
#include "etl/string.h"

namespace gui::common
{

class DebugLogViewModel
{
public:
    static constexpr std::size_t kMaxLineLength = 256U;
    static constexpr std::size_t kMaxLogLines = 200U;

    using LogLine = etl::string<kMaxLineLength>;
    using UpdateCallback = etl::delegate<void()>;

    void setUpdateCallback(UpdateCallback callback);

    void addLogLine(const char *line);
    void addLogLine(const LogLine &line);
    void clear();

    std::vector<LogLine> getLogLines() const;

private:
    mutable std::mutex mutex_;
    std::vector<LogLine> log_lines_;
    UpdateCallback update_callback_;
};

} // namespace gui::common
