#include "usb_midi_host.h"

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tusb.h"

#include <atomic>

namespace
{

constexpr const char *TAG = "UsbMidiHost";
constexpr int kTaskStackSize = 4096;
constexpr int kTaskPriority = 5;
constexpr int kTaskCore = 1;
constexpr int kQueueDepth = 32;

usb_phy_handle_t s_usbPhyHandle = nullptr;
QueueHandle_t s_midiRxQueue = nullptr;
QueueHandle_t s_midiTxQueue = nullptr;
gui::esp32::MidiByteStreamDecoder s_decoder;
std::atomic<bool> s_deviceMounted{false};

std::uint8_t encodeMidiStatusByte(midi2pwm::midi::ChannelMessageType type, std::uint8_t channel)
{
    std::uint8_t statusNibble = 0;
    switch (type) {
    case midi2pwm::midi::ChannelMessageType::NoteOff:              statusNibble = 0x80; break;
    case midi2pwm::midi::ChannelMessageType::NoteOn:               statusNibble = 0x90; break;
    case midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure:statusNibble = 0xA0; break;
    case midi2pwm::midi::ChannelMessageType::ControlChange:        statusNibble = 0xB0; break;
    case midi2pwm::midi::ChannelMessageType::ProgramChange:        statusNibble = 0xC0; break;
    case midi2pwm::midi::ChannelMessageType::ChannelPressure:      statusNibble = 0xD0; break;
    case midi2pwm::midi::ChannelMessageType::PitchBend:            statusNibble = 0xE0; break;
    default: statusNibble = 0x80; break;
    }
    return statusNibble | (channel & 0x0F);
}

std::uint8_t expectedDataBytes(midi2pwm::midi::ChannelMessageType type)
{
    switch (type) {
    case midi2pwm::midi::ChannelMessageType::ProgramChange:
    case midi2pwm::midi::ChannelMessageType::ChannelPressure:
        return 1;
    default:
        return 2;
    }
}

void usbHostTask(void *)
{
    ESP_LOGI(TAG, "USB host task started");

    gui::esp32::ParsedMidiMessage txMsg;

    while (true) {
        tuh_task();

        if (s_deviceMounted.load(std::memory_order_relaxed)) {
            while (xQueueReceive(s_midiTxQueue, &txMsg, 0) == pdTRUE) {
                std::uint8_t status = encodeMidiStatusByte(txMsg.type, txMsg.channel);
                std::uint8_t dataCount = expectedDataBytes(txMsg.type);
                std::uint8_t buf[3] = {status, txMsg.data1, txMsg.data2};
                tuh_midi_stream_write(0, 0, buf, 1 + dataCount);
            }
            tuh_midi_write_flush(0);
        }
    }
}

bool initUsbPhy()
{
    usb_phy_config_t phyConfig = {};
    phyConfig.controller = USB_PHY_CTRL_OTG;
    phyConfig.target = USB_PHY_TARGET_INT;
    phyConfig.otg_mode = USB_OTG_MODE_HOST;
    phyConfig.otg_speed = USB_PHY_SPEED_UNDEFINED;

    esp_err_t err = usb_new_phy(&phyConfig, &s_usbPhyHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB PHY init failed: %d", err);
        return false;
    }

    ESP_LOGI(TAG, "USB PHY initialized in host mode");
    return true;
}

} // namespace

extern "C" {

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mountData)
{
    ESP_LOGI(TAG, "MIDI device mounted idx=%u addr=%u rx_cables=%u tx_cables=%u",
             idx, mountData->daddr, mountData->rx_cable_count, mountData->tx_cable_count);
    s_deviceMounted.store(true, std::memory_order_relaxed);
}

void tuh_midi_umount_cb(uint8_t idx)
{
    ESP_LOGI(TAG, "MIDI device unmounted idx=%u", idx);
    s_deviceMounted.store(false, std::memory_order_relaxed);
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferredBytes)
{
    if (xferredBytes == 0) {
        return;
    }

    uint8_t cableNum = 0;
    uint8_t buffer[48];

    while (true) {
        uint32_t bytesRead = tuh_midi_stream_read(idx, &cableNum, buffer, sizeof(buffer));
        if (bytesRead == 0) {
            break;
        }

        for (uint32_t i = 0; i < bytesRead; ++i) {
            s_decoder.pushByte(buffer[i]);
            auto msg = s_decoder.getMessage();
            if (msg.has_value()) {
                xQueueSend(s_midiRxQueue, &msg.value(), 0);
            }
        }
    }
}

void tuh_midi_tx_cb(uint8_t, uint32_t) {}

} // extern "C"

namespace gui::esp32
{

bool initUsbMidiHost()
{
    s_midiRxQueue = xQueueCreate(kQueueDepth, sizeof(ParsedMidiMessage));
    s_midiTxQueue = xQueueCreate(kQueueDepth, sizeof(ParsedMidiMessage));

    if (!s_midiRxQueue || !s_midiTxQueue) {
        ESP_LOGE(TAG, "Failed to create MIDI queues");
        return false;
    }

    if (!initUsbPhy()) {
        return false;
    }

    tusb_rhport_init_t hostInit = {};
    hostInit.role = TUSB_ROLE_HOST;
    hostInit.speed = TUSB_SPEED_AUTO;

    if (!tusb_init(BOARD_TUH_RHPORT, &hostInit)) {
        ESP_LOGE(TAG, "TinyUSB host init failed");
        return false;
    }

    ESP_LOGI(TAG, "TinyUSB host stack initialized");

    BaseType_t result = xTaskCreatePinnedToCore(
        usbHostTask, "usb_host", kTaskStackSize, nullptr, kTaskPriority, nullptr, kTaskCore);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB host task");
        return false;
    }

    return true;
}

bool sendToUsbMidi(const ParsedMidiMessage &msg)
{
    return xQueueSend(s_midiTxQueue, &msg, 0) == pdTRUE;
}

bool pollUsbMidiRx(ParsedMidiMessage &msg)
{
    return xQueueReceive(s_midiRxQueue, &msg, 0) == pdTRUE;
}

} // namespace gui::esp32
