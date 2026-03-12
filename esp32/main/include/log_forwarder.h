#pragma once

namespace gui::esp32
{

void initLogForwarder(bool alsoMirrorToConsole = false);
void deinitLogForwarder();

} // namespace gui::esp32
