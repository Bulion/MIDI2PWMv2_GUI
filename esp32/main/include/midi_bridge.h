#pragma once

#include "gui_common/message_processor.h"

namespace gui::esp32
{

void initMidiBridge(gui::common::MessageProcessor &processor);
void pollMidiBridge();

} // namespace gui::esp32
