#include "ui_callbacks.h"

#include "app-window.h"
#include "gui_common/midi_note_util.h"
#include "gui_common/slint_adapters.h"

namespace gui::esp32
{

void registerUiCallbacks(slint::ComponentHandle<AppWindow> app,
                         gui::common::CommLoop &commLoop,
                         MidiAssignmentState &assignState)
{
    app->on_save_channel_config([&commLoop](int channelIdx, slint::SharedString noteStr, ModeConfig modeConfig) {
        uint16_t noteNumber = 255;
        if (!noteStr.empty()) {
            noteNumber = gui::common::parseNoteString(std::string{noteStr});
        }
        uint16_t noteNumberB = 255;
        std::string noteBStr{modeConfig.note_b};
        if (!noteBStr.empty()) {
            noteNumberB = gui::common::parseNoteString(noteBStr);
        }
        auto config = gui::common::buildChannelConfigFromModeConfig(channelIdx, noteNumber, noteNumberB, modeConfig);
        commLoop.post(gui::common::CommLoop::SendConfigCmd{std::move(config)});
    });

    app->on_assign_note_clicked([&assignState](int channelIdx) {
        assignState.isAssigningNote.store(true);
        assignState.channelAwaitingNote.store(channelIdx);
    });

    app->on_note_assigned_from_backend([app](slint::SharedString noteStr) {
        app->set_temp_note(noteStr);
        app->set_temp_is_assigning(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_assign_note_b_clicked([&assignState](int channelIdx) {
        assignState.isAssigningNoteB.store(true);
        assignState.channelAwaitingNoteB.store(channelIdx);
    });

    app->on_note_b_assigned_from_backend([app](slint::SharedString noteStr) {
        app->set_temp_note_b(noteStr);
        app->set_temp_is_assigning_b(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_assign_cc_clicked([&assignState]() {
        assignState.isAssigningCc.store(true);
    });

    app->on_cc_assigned_from_backend([app](int ccNumber) {
        auto tempConfig = app->get_temp_mode_config();
        tempConfig.cc_data.cc_number = ccNumber;
        app->set_temp_mode_config(tempConfig);
        app->set_temp_is_assigning_cc(false);
        app->set_temp_popup_dirty(true);
    });

    app->on_reset_note_clicked([](int) {});
    app->on_reset_note_b_clicked([](int) {});
    app->on_reset_cc_clicked([app]() {
        auto tempConfig = app->get_temp_mode_config();
        tempConfig.cc_data.cc_number = 0;
        app->set_temp_mode_config(tempConfig);
        app->set_temp_popup_dirty(true);
    });

    app->on_reset_fault_clicked([&commLoop](int channelIdx) {
        commLoop.post(gui::common::CommLoop::ResetFaultCmd{static_cast<std::uint16_t>(channelIdx)});
    });

    app->on_popup_closed([&assignState, app]() {
        if (assignState.isAssigningNote.load()) {
            assignState.isAssigningNote.store(false);
            assignState.channelAwaitingNote.store(-1);
        }
        if (assignState.isAssigningNoteB.load()) {
            assignState.isAssigningNoteB.store(false);
            assignState.channelAwaitingNoteB.store(-1);
        }
        if (assignState.isAssigningCc.load()) {
            assignState.isAssigningCc.store(false);
            app->set_temp_is_assigning_cc(false);
        }
    });

    app->on_refresh_ports([]() {});
    app->on_connect_port([](const slint::SharedString &) {});
}

} // namespace gui::esp32
