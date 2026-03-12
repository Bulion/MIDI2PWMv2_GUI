#include "gui_common/midi_note_util.h"

#include <cstdlib>
#include <utility>

namespace gui::common
{

std::string midiNoteToString(uint16_t noteNumber)
{
    static const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (noteNumber / 12) - 1;
    int noteIdx = noteNumber % 12;
    return std::string(NOTE_NAMES[noteIdx]) + std::to_string(octave);
}

uint16_t parseNoteString(const std::string &noteStr)
{
    if (noteStr.empty() || noteStr == "---") {
        return 255;
    }

    static const std::pair<const char *, int> NOTE_MAP[] = {
        {"C#", 1}, {"D#", 3}, {"F#", 6}, {"G#", 8}, {"A#", 10},
        {"C", 0}, {"D", 2}, {"E", 4}, {"F", 5}, {"G", 7}, {"A", 9}, {"B", 11}
    };

    size_t octavePos = noteStr.find_first_of("0123456789-");
    if (octavePos == std::string::npos || octavePos == 0) {
        return 255;
    }

    std::string noteName = noteStr.substr(0, octavePos);
    int octave = std::atoi(noteStr.c_str() + octavePos);

    int noteIdx = -1;
    for (const auto &[name, idx] : NOTE_MAP) {
        if (noteName == name) {
            noteIdx = idx;
            break;
        }
    }

    if (noteIdx < 0) {
        return 255;
    }

    int midiNote = (octave + 1) * 12 + noteIdx;
    if (midiNote < 0 || midiNote > 127) {
        return 255;
    }

    return static_cast<uint16_t>(midiNote);
}

} // namespace gui::common
