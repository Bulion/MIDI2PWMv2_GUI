#pragma once

#include "midi_assignment_state.h"

#include "gui_common/comm_loop.h"

#include <slint.h>

class AppWindow;

namespace gui::esp32
{

void registerUiCallbacks(slint::ComponentHandle<AppWindow> app,
                         gui::common::CommLoop &commLoop,
                         MidiAssignmentState &assignState);

} // namespace gui::esp32
