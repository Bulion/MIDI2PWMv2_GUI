#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gui::esp32
{

using MidiRxCallback = std::function<void(const std::uint8_t *data, std::size_t size)>;

bool initUsbMidiHost(MidiRxCallback onMidiReceived);

} // namespace gui::esp32
