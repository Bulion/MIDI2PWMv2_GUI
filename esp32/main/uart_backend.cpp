#include "uart_backend.h"

#include "gui_common/log.h"
#include "driver/uart.h"
#include "etl/array.h"

namespace gui::esp32
{

namespace
{
constexpr int kBufferSize = 2048;
constexpr int kTaskStackSize = 4096;
constexpr int kTaskPriority = 5;
constexpr int kTaskCore = 0;
constexpr int kReadTimeoutMs = 50;

#ifdef CONFIG_ESP_CONSOLE_UART_NUM
constexpr uart_port_t kConsoleUart = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
#else
constexpr uart_port_t kConsoleUart = UART_NUM_0;
#endif

} // namespace

bool UartBackend::initialize()
{
    uart_config_t uartConfiguration{};
    uartConfiguration.baud_rate = 921600;
    uartConfiguration.data_bits = UART_DATA_8_BITS;
    uartConfiguration.parity = UART_PARITY_DISABLE;
    uartConfiguration.stop_bits = UART_STOP_BITS_1;
    uartConfiguration.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uartConfiguration.source_clk = UART_SCLK_APB;

    bool uartIsUsedByConsole = (kUartPort == kConsoleUart);
    if (uartIsUsedByConsole) {
        uart_driver_delete(kUartPort);
    }

    constexpr int NO_TRANSMIT_BUFFER = 0;
    constexpr int NO_EVENT_QUEUE = 0;
    esp_err_t driverInstallResult =
        uart_driver_install(kUartPort, kBufferSize, NO_TRANSMIT_BUFFER, NO_EVENT_QUEUE, nullptr, 0);
    if (driverInstallResult != ESP_OK) {
        GUI_LOG_ERROR("UartBackend", "Failed to install UART driver: %d", driverInstallResult);
        return false;
    }

    esp_err_t paramConfigResult = uart_param_config(kUartPort, &uartConfiguration);
    if (paramConfigResult != ESP_OK) {
        GUI_LOG_ERROR("UartBackend", "Failed to configure UART parameters: %d", paramConfigResult);
        uart_driver_delete(kUartPort);
        return false;
    }

    esp_err_t pinConfigResult = uart_set_pin(kUartPort, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (pinConfigResult != ESP_OK) {
        GUI_LOG_ERROR("UartBackend", "Failed to configure UART pins: %d", pinConfigResult);
        uart_driver_delete(kUartPort);
        return false;
    }

    disconnectCallbackPending_ = true;

    if (!startFreeRtosReaderTask()) {
        uart_driver_delete(kUartPort);
        return false;
    }

    return true;
}

UartBackend::~UartBackend()
{
    readerTaskIsRunning_ = false;
    stopFreeRtosReaderTask();
}

std::vector<PortInfo> UartBackend::refreshPorts()
{
    return {};
}

bool UartBackend::connect([[maybe_unused]] const std::string &portIdentifier)
{
    return readerTaskIsRunning_.load() && (readerTaskHandle_ != nullptr);
}

void UartBackend::disconnect()
{
}

bool UartBackend::isConnected() const
{
    return readerTaskIsRunning_.load();
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
    if (!readerTaskIsRunning_.load()) {
        return false;
    }

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

bool UartBackend::startFreeRtosReaderTask()
{
    if (readerTaskHandle_ != nullptr) {
        return true;
    }

    constexpr const char *UART_READER_TASK_NAME = "uart_rx";
    const BaseType_t taskCreationResult = xTaskCreatePinnedToCore(
        &UartBackend::FreeRtosTaskEntryPoint,
        UART_READER_TASK_NAME,
        kTaskStackSize,
        this,
        kTaskPriority,
        &readerTaskHandle_,
        kTaskCore);

    if (taskCreationResult != pdPASS) {
        GUI_LOG_ERROR("UartBackend", "Failed to create UART reader task");
        readerTaskHandle_ = nullptr;
        return false;
    }

    readerTaskIsRunning_ = true;
    return true;
}

void UartBackend::stopFreeRtosReaderTask()
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
        uart_driver_delete(kUartPort);
    }
}

void UartBackend::FreeRtosTaskEntryPoint(void *taskParameterPointer)
{
    auto *backendInstance = static_cast<UartBackend *>(taskParameterPointer);

    bool instancePointerIsValid = (backendInstance != nullptr);
    if (instancePointerIsValid) {
        backendInstance->uartReaderTaskLoop();
    }

    vTaskDelete(nullptr);
}

void UartBackend::uartReaderTaskLoop()
{
    constexpr size_t RECEIVE_BUFFER_SIZE_BYTES = 256;
    etl::array<std::uint8_t, RECEIVE_BUFFER_SIZE_BYTES> receiveBuffer{};

    while (readerTaskIsRunning_.load()) {
        TickType_t readTimeoutTicks = pdMS_TO_TICKS(kReadTimeoutMs);
        int bytesReadOrError = uart_read_bytes(kUartPort, receiveBuffer.data(), receiveBuffer.size(), readTimeoutTicks);

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
                size_t bytesReadCount = static_cast<std::size_t>(bytesReadOrError);
                localCallbackCopy(receiveBuffer.data(), bytesReadCount);
            }
        }
    }

    uart_driver_delete(kUartPort);

    readerTaskHandle_ = nullptr;

    bool callbackWasPending = disconnectCallbackPending_.exchange(false);
    if (callbackWasPending) {
        std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);

        if (connectionLostCallback_ != nullptr) {
            connectionLostCallback_();
        }
    }
}

} // namespace gui::esp32
