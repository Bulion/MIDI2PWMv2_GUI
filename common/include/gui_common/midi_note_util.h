#pragma once

#include <cstdint>
#include <string>

namespace gui::common
{

std::string midiNoteToString(uint16_t noteNumber);
uint16_t parseNoteString(const std::string &noteStr);

} // namespace gui::common
