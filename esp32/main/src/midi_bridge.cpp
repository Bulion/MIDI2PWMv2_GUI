#include "midi_bridge.h"

#include "usb_midi_host.h"

#include "esp_log.h"

namespace gui::esp32
{

namespace
{

constexpr const char *TAG = "MidiBridge";

gui::common::MessageProcessor *s_processor = nullptr;

void forwardToUsbMidi(const midi2pwm::midi::ChannelMessageT &msg)
{
    ParsedMidiMessage parsed;
    parsed.type = msg.message_type;
    parsed.channel = msg.channel;
    parsed.data1 = msg.data1;
    parsed.data2 = msg.data2;
    sendToUsbMidi(parsed);
}

} // namespace

void initMidiBridge(gui::common::MessageProcessor &processor)
{
    s_processor = &processor;

    processor.setMidiForwardCallback(
        gui::common::MessageProcessor::MidiForwardCallback::create<forwardToUsbMidi>());

    ESP_LOGI(TAG, "MIDI bridge initialized");
}

void pollMidiBridge()
{
    if (!s_processor) {
        return;
    }

    ParsedMidiMessage msg;
    while (pollUsbMidiRx(msg)) {
        s_processor->sendMidiChannelMessage(msg.type, msg.channel, msg.data1, msg.data2);
    }
}

} // namespace gui::esp32
