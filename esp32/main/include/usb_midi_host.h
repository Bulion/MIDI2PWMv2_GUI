#pragma once

#include "midi_byte_stream_decoder.h"

namespace gui::esp32
{

bool initUsbMidiHost();
bool sendToUsbMidi(const ParsedMidiMessage &msg);
bool pollUsbMidiRx(ParsedMidiMessage &msg);

} // namespace gui::esp32
