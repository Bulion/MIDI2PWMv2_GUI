#include "ota_handler.h"

#include "esp32_flash_writer.h"
#include "gui_common/log.h"
#include "psram_buffered_flash_writer.h"

#include "libcomm/frame_transport.h"
#include "libcomm/ota_manager.h"

#include <driver/uart.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace gui::esp32
{

namespace
{

constexpr const char *TAG = "OtaHandler";

static Esp32FlashWriter s_esp32FlashWriterDelegate;
static PsramBufferedFlashWriter s_psramFlashWriter(s_esp32FlashWriterDelegate);
static gui::common::ConnectionViewModel *s_otaConnectionVm = nullptr;
static UartBackend *s_echoBackend = nullptr;

static bool echoWrite(const std::uint8_t *data, std::size_t size)
{
    return s_echoBackend ? s_echoBackend->write(data, size) : false;
}

static libcomm::FrameTransport s_echoTransport(
    libcomm::FrameTransport::WriteCallback::create<&echoWrite>());

static bool echoFrame(const std::uint8_t *data, std::size_t size)
{
    return s_echoTransport.Send(data, size);
}

static OtaDisplayState s_otaDisplay;

static void otaSendProgress(midi2pwm::ota::Target target, midi2pwm::ota::OtaStatus status,
                            std::uint16_t chunksReceived, std::uint16_t totalChunks,
                            const char *errorMessage)
{
    {
        std::lock_guard lock(s_otaDisplay.mutex);
        s_otaDisplay.status = status;
        s_otaDisplay.chunksReceived = chunksReceived;
        s_otaDisplay.totalChunks = totalChunks;
    }
    s_otaDisplay.version.fetch_add(1, std::memory_order_release);

    if (s_otaConnectionVm) {
        s_otaConnectionVm->messageProcessor().sendOtaProgress(
            target, status, chunksReceived, totalChunks, errorMessage);
    }
}

static libcomm::OtaManager s_otaManager(
    s_psramFlashWriter,
    midi2pwm::ota::Target::Esp32,
    libcomm::OtaManager::SendProgressCallback::create<&otaSendProgress>());

constexpr uint32_t OTA_DATA_TIMEOUT_MS = 30000;
static std::atomic<int64_t> s_lastOtaActivityUs{0};

static void otaTouchActivity()
{
    s_lastOtaActivityUs.store(esp_timer_get_time(), std::memory_order_release);
}

static void onOtaBegin(const midi2pwm::ota::OtaBegin &msg)
{
    otaTouchActivity();
    {
        std::lock_guard lock(s_otaDisplay.mutex);
        s_otaDisplay.compressedSize = msg.compressed_size();
        s_otaDisplay.firmwareSize = msg.firmware_size();
        s_otaDisplay.totalChunks = msg.total_chunks();
    }
    s_psramFlashWriter.setCompressedTransfer(
        msg.compressed_size(), msg.compressed_crc32(), msg.firmware_size());
    s_otaManager.handleBegin(msg);
}

static void onOtaData(const midi2pwm::ota::OtaData &msg)
{
    otaTouchActivity();
    s_otaManager.handleData(msg);
    s_otaDisplay.chunksReceived = msg.chunk_index() + 1;
    s_otaDisplay.version.fetch_add(1, std::memory_order_release);
}

static void otaEndTask(void *)
{
    s_otaManager.handleEnd();
    if (s_otaManager.status() == midi2pwm::ota::OtaStatus::Rebooting) {
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(2000));
        esp_restart();
    }
    vTaskDelete(nullptr);
}

static void onOtaEnd(const midi2pwm::ota::OtaEnd &)
{
    otaTouchActivity();
    xTaskCreatePinnedToCore(otaEndTask, "ota_end", 8192, nullptr, 3, nullptr, 1);
}

static void onOtaAbort(const midi2pwm::ota::OtaAbort &msg)
{
    s_otaManager.handleAbort(msg);
    s_lastOtaActivityUs.store(0, std::memory_order_release);
    {
        std::lock_guard lock(s_otaDisplay.mutex);
        s_otaDisplay.status = midi2pwm::ota::OtaStatus::Error;
    }
    s_otaDisplay.version.fetch_add(1, std::memory_order_release);
}

} // namespace

void initOtaHandler(gui::common::ConnectionViewModel &vm, UartBackend &backend)
{
    s_otaConnectionVm = &vm;
    s_echoBackend = &backend;
}

void registerOtaCallbacks(gui::common::MessageProcessor &msgProc)
{
    msgProc.setUnknownFrameCallback(
        gui::common::MessageProcessor::UnknownFrameCallback::create<&echoFrame>());
    msgProc.setOtaBeginCallback(
        gui::common::MessageProcessor::OtaBeginCallback::create<&onOtaBegin>());
    msgProc.setOtaDataCallback(
        gui::common::MessageProcessor::OtaDataCallback::create<&onOtaData>());
    msgProc.setOtaEndCallback(
        gui::common::MessageProcessor::OtaEndCallback::create<&onOtaEnd>());
    msgProc.setOtaAbortCallback(
        gui::common::MessageProcessor::OtaAbortCallback::create<&onOtaAbort>());
}

void checkOtaDataTimeout()
{
    auto status = s_otaManager.status();
    if (status != midi2pwm::ota::OtaStatus::Receiving) {
        return;
    }

    int64_t lastActivity = s_lastOtaActivityUs.load(std::memory_order_acquire);
    if (lastActivity == 0) {
        return;
    }

    int64_t nowUs = esp_timer_get_time();
    int64_t elapsedMs = (nowUs - lastActivity) / 1000;
    if (elapsedMs > OTA_DATA_TIMEOUT_MS) {
        GUI_LOG_ERROR(TAG, "OTA data timeout after %lld ms", static_cast<long long>(elapsedMs));
        s_otaManager.handleAbort("Data reception timeout");
        s_lastOtaActivityUs.store(0, std::memory_order_release);
    }
}

OtaDisplayState &otaDisplayState()
{
    return s_otaDisplay;
}

} // namespace gui::esp32
