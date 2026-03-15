#pragma once

#include "gui_common/message_processor.h"
#include "gui_common/view_models/midi_message_view_model.h"

namespace gui::esp32
{

void initMidiBridge(gui::common::MessageProcessor &processor,
                    gui::common::MidiMessageViewModel &midiViewModel);
void pollMidiBridge();

} // namespace gui::esp32
