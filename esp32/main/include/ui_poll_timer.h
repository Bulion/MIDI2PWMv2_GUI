#pragma once

#include "midi_assignment_state.h"

#include "gui_common/comm_loop.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"

#include <slint.h>

class AppWindow;

namespace gui::esp32
{

void startUiPollTimer(slint::ComponentHandle<AppWindow> app,
                      gui::common::CommLoop &commLoop,
                      gui::common::ConnectionViewModel &connVm,
                      gui::common::ChannelTelemetryViewModel &telVm,
                      gui::common::MidiMessageViewModel &midiVm,
                      MidiAssignmentState &assignState);

} // namespace gui::esp32
