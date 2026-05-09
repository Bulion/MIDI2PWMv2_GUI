#pragma once

#include <atomic>

namespace gui::esp32
{

struct MidiAssignmentState
{
    std::atomic<bool> isAssigningNote{false};
    std::atomic<int> channelAwaitingNote{-1};
    std::atomic<bool> isAssigningNoteB{false};
    std::atomic<int> channelAwaitingNoteB{-1};
    std::atomic<bool> isAssigningCc{false};
};

} // namespace gui::esp32
