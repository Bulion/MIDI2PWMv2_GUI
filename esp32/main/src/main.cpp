#include "app-window.h"
#include "comm_task.h"
#include "display_init.h"
#include "log_forwarder.h"
#include "midi_assignment_state.h"
#include "midi_bridge.h"
#include "ota_handler.h"
#include "uart_backend.h"
#include "ui_callbacks.h"
#include "ui_poll_timer.h"
#include "usb_midi_host.h"

#include "gui_common/comm_loop.h"
#include "gui_common/log.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"

#include <cstdlib>

using gui::common::ChannelTelemetryViewModel;
using gui::common::ConnectionViewModel;
using gui::common::MidiMessageViewModel;

extern "C" void app_main(void)
{
    gui::esp32::initializeDisplay();
    gui::common::InitializeLibcommLogging();

    auto app = AppWindow::create();

    MidiMessageViewModel midiViewModel;
    ChannelTelemetryViewModel channelTelemetryViewModel;

    gui::esp32::UartBackend backend;
    if (!backend.initialize()) {
        abort();
    }

    gui::esp32::initUsbMidiHost();

    ConnectionViewModel connectionViewModel{backend, midiViewModel, channelTelemetryViewModel};
    gui::common::CommLoop commLoop(connectionViewModel);

    gui::esp32::initLogForwarder(connectionViewModel.messageProcessor(), false);
    gui::esp32::initMidiBridge(connectionViewModel.messageProcessor(), midiViewModel);
    gui::esp32::initOtaHandler(connectionViewModel, backend);
    gui::esp32::registerOtaCallbacks(connectionViewModel.messageProcessor());
    gui::esp32::startCommTask(commLoop);

    commLoop.post(gui::common::CommLoop::ConnectCmd{"UART0"});

    gui::esp32::MidiAssignmentState assignState;
    gui::esp32::registerUiCallbacks(app, commLoop, assignState);

    app->set_show_connection_screen(false);
    app->set_waiting_for_device_data(true);
    app->set_midi_message("No midi message received yet");

    gui::esp32::startUiPollTimer(app, commLoop, connectionViewModel,
                                 channelTelemetryViewModel, midiViewModel, assignState);
    app->run();
}
