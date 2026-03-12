#include "usb_midi_host.h"

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

namespace
{

constexpr const char *TAG = "UsbMidiHost";
constexpr int kTaskStackSize = 4096;
constexpr int kTaskPriority = 5;
constexpr int kTaskCore = 1;

gui::esp32::MidiRxCallback s_midiRxCallback;
usb_phy_handle_t s_usbPhyHandle = nullptr;

void usbHostTask(void *)
{
    ESP_LOGI(TAG, "USB host task started");

    while (true) {
        tuh_task();
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
}

void tuh_midi_umount_cb(uint8_t idx)
{
    ESP_LOGI(TAG, "MIDI device unmounted idx=%u", idx);
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

        if (s_midiRxCallback) {
            s_midiRxCallback(buffer, bytesRead);
        }
    }
}

void tuh_midi_tx_cb(uint8_t, uint32_t) {}

} // extern "C"

namespace gui::esp32
{

bool initUsbMidiHost(MidiRxCallback onMidiReceived)
{
    s_midiRxCallback = std::move(onMidiReceived);

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

} // namespace gui::esp32
