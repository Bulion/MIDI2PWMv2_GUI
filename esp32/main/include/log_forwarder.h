#pragma once

#include "gui_common/message_processor.h"

namespace gui::esp32
{

void initLogForwarder(gui::common::MessageProcessor &messageProcessor,
                      bool alsoMirrorToConsole = false);
void deinitLogForwarder();

} // namespace gui::esp32
