#include <slint.h>

#include "app-window.h"

int main()
{
    auto app = AppWindow::create();

    app->set_midi_message("Mocked MIDI: Note On C4 velocity 100");

    app->run();
    return 0;
}
