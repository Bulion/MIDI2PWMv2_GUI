#include "uart_backend.h"

#include "gui_common/log.h"
#include "driver/uart.h"
#include "etl/array.h"

namespace gui::esp32
{

namespace
{
constexpr int kBufferSize = 8192;
constexpr int kTaskStackSize = 8192;
constexpr int kTaskPriority = 5;
constexpr int kTaskCore = 0;
constexpr int kReadTimeoutMs = 50;

} // namespace

bool UartBackend::initialize()
{
    uart_config_t uartConfiguration{};
    uartConfiguration.baud_rate = 921600;
    uartConfiguration.data_bits = UART_DATA_8_BITS;
    uartConfiguration.parity = UART_PARITY_DISABLE;
    uartConfiguration.stop_bits = UART_STOP_BITS_1;
    uartConfiguration.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uartConfiguration.source_clk = UART_SCLK_DEFAULT;

    uart_driver_delete(kUartPort);
    uart_driver_install(kUartPort, kBufferSize, 0, 0, nullptr, 0);
    uart_param_config(kUartPort, &uartConfiguration);

    initialized_ = true;
    return true;
}

UartBackend::~UartBackend()
{
    readerTaskIsRunning_ = false;
    stopReaderTask();

    if (initialized_.load()) {
        uart_driver_delete(kUartPort);
        initialized_ = false;
    }
}

std::vector<PortInfo> UartBackend::refreshPorts()
{
    return {};
}

bool UartBackend::connect([[maybe_unused]] const std::string &portIdentifier)
{
    if (!initialized_.load()) {
        return false;
    }

    if (readerTaskHandle_ == nullptr) {
        return startReaderTask();
    }

    return true;
}

void UartBackend::disconnect()
{
}

bool UartBackend::isConnected() const
{
    return initialized_.load();
}

void UartBackend::setDataCallback(DataCallback dataReceivedCallback)
{
    std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
    dataReceivedCallback_ = dataReceivedCallback;
}

void UartBackend::setDisconnectCallback(DisconnectCallback connectionLostCallback)
{
    std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
    connectionLostCallback_ = connectionLostCallback;
}

bool UartBackend::write(const std::uint8_t *data, std::size_t size)
{
    if (!initialized_.load()) {
        return false;
    }

    std::lock_guard<std::mutex> writeLock(writeAccessMutex_);
    int bytesWrittenOrError = uart_write_bytes(kUartPort, data, size);

    if (bytesWrittenOrError < 0) {
        GUI_LOG_ERROR("UartBackend", "UART write error: %d", bytesWrittenOrError);
        return false;
    }

    if (static_cast<std::size_t>(bytesWrittenOrError) != size) {
        GUI_LOG_WARNING("UartBackend", "Partial UART write: %d/%zu bytes", bytesWrittenOrError, size);
        return false;
    }

    return true;
}

bool UartBackend::startReaderTask()
{
    if (readerTaskHandle_ != nullptr) {
        return true;
    }

    readerTaskIsRunning_ = true;

    BaseType_t taskCreationResult = xTaskCreatePinnedToCore(
        &UartBackend::FreeRtosTaskEntryPoint,
        "uart_rx",
        kTaskStackSize,
        this,
        kTaskPriority,
        &readerTaskHandle_,
        kTaskCore);

    if (taskCreationResult != pdPASS) {
        GUI_LOG_ERROR("UartBackend", "Failed to create UART reader task");
        readerTaskIsRunning_ = false;
        readerTaskHandle_ = nullptr;
        return false;
    }

    return true;
}

void UartBackend::stopReaderTask()
{
    if (readerTaskHandle_ == nullptr) {
        return;
    }

    constexpr int MAX_WAIT_MS = 1000;
    constexpr int POLL_INTERVAL_MS = 10;
    int waitedMs = 0;

    while (readerTaskHandle_ != nullptr && waitedMs < MAX_WAIT_MS) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        waitedMs += POLL_INTERVAL_MS;
    }

    if (readerTaskHandle_ != nullptr) {
        GUI_LOG_WARNING("UartBackend", "Reader task did not exit gracefully, forcing deletion");
        vTaskDelete(readerTaskHandle_);
        readerTaskHandle_ = nullptr;
    }
}

void UartBackend::FreeRtosTaskEntryPoint(void *taskParameterPointer)
{
    auto *backendInstance = static_cast<UartBackend *>(taskParameterPointer);

    if (backendInstance != nullptr) {
        backendInstance->uartReaderTaskLoop();
    }

    vTaskDelete(nullptr);
}

void UartBackend::uartReaderTaskLoop()
{
    constexpr size_t RECEIVE_BUFFER_SIZE_BYTES = 1024;
    etl::array<std::uint8_t, RECEIVE_BUFFER_SIZE_BYTES> receiveBuffer{};

    while (readerTaskIsRunning_.load()) {
        int bytesReadOrError = uart_read_bytes(
            kUartPort, receiveBuffer.data(), receiveBuffer.size(), pdMS_TO_TICKS(kReadTimeoutMs));

        if (bytesReadOrError < 0) {
            GUI_LOG_ERROR("UartBackend", "UART read error: %d", bytesReadOrError);
            continue;
        }

        if (bytesReadOrError > 0) {
            gui::common::ConnectionBackend::DataCallback localCallbackCopy;
            {
                std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
                localCallbackCopy = dataReceivedCallback_;
            }

            if (localCallbackCopy != nullptr) {
                auto bytesReadCount = static_cast<std::size_t>(bytesReadOrError);
                localCallbackCopy(receiveBuffer.data(), bytesReadCount);
            }
        }
    }

    readerTaskHandle_ = nullptr;
}

} // namespace gui::esp32
