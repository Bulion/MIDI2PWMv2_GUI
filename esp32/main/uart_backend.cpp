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

UartBackend::UartBackend() = default;

UartBackend::~UartBackend()
{
    disconnect();
}

std::vector<std::string> UartBackend::refreshPorts()
{
    std::vector<std::string> availableUartPorts;
    availableUartPorts.emplace_back("UART0");
    return availableUartPorts;
}

bool UartBackend::connect([[maybe_unused]] const std::string &portIdentifier)
{
    bool alreadyConnected = readerTaskIsRunning_.load();
    if (alreadyConnected) {
        return true;
    }

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
    bool driverInstallationSucceeded =
        (uart_driver_install(kUartPort, kBufferSize, NO_TRANSMIT_BUFFER, NO_EVENT_QUEUE, nullptr, 0) == ESP_OK);
    if (!driverInstallationSucceeded) {
        GUI_LOG_ERROR("UartBackend", "Failed to install UART driver");
        return false;
    }

    bool parameterConfigurationSucceeded = (uart_param_config(kUartPort, &uartConfiguration) == ESP_OK);
    if (!parameterConfigurationSucceeded) {
        GUI_LOG_ERROR("UartBackend", "Failed to configure UART parameters");
        uart_driver_delete(kUartPort);
        return false;
    }

    bool pinConfigurationSucceeded =
        (uart_set_pin(kUartPort, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK);
    if (!pinConfigurationSucceeded) {
        GUI_LOG_ERROR("UartBackend", "Failed to configure UART pins");
        uart_driver_delete(kUartPort);
        return false;
    }

    readerTaskIsRunning_ = true;
    disconnectCallbackPending_ = true;

    return startFreeRtosReaderTask();
}

void UartBackend::disconnect()
{
    bool notConnected = !readerTaskIsRunning_.load() && (readerTaskHandle_ == nullptr);
    if (notConnected) {
        return;
    }

    readerTaskIsRunning_ = false;
    stopFreeRtosReaderTask();
    uart_driver_delete(kUartPort);

    bool callbackWasPending = disconnectCallbackPending_.exchange(false);
    if (callbackWasPending) {
        std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);

        bool disconnectCallbackIsRegistered = (connectionLostCallback_ != nullptr);
        if (disconnectCallbackIsRegistered) {
            connectionLostCallback_();
        }
    }
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

bool UartBackend::startFreeRtosReaderTask()
{
    bool taskIsAlreadyRunning = (readerTaskHandle_ != nullptr);
    if (taskIsAlreadyRunning) {
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

    bool taskCreationSucceeded = (taskCreationResult == pdPASS);
    if (!taskCreationSucceeded) {
        GUI_LOG_ERROR("UartBackend", "Failed to create UART reader task");
        readerTaskHandle_ = nullptr;
        uart_driver_delete(kUartPort);
        readerTaskIsRunning_ = false;
        return false;
    }

    return true;
}

void UartBackend::stopFreeRtosReaderTask()
{
    bool taskIsRunning = (readerTaskHandle_ != nullptr);
    if (taskIsRunning) {
        TaskHandle_t taskHandleToDelete = readerTaskHandle_;
        readerTaskHandle_ = nullptr;
        vTaskDelete(taskHandleToDelete);
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

        bool dataWasSuccessfullyRead = (bytesReadOrError > 0);
        if (dataWasSuccessfullyRead) {
            gui::common::ConnectionBackend::DataCallback localCallbackCopy;
            {
                std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
                localCallbackCopy = dataReceivedCallback_;
            }

            bool callbackIsRegistered = (localCallbackCopy != nullptr);
            if (callbackIsRegistered) {
                size_t bytesReadCount = static_cast<std::size_t>(bytesReadOrError);
                localCallbackCopy(receiveBuffer.data(), bytesReadCount);
            }
        }
    }

    uart_driver_delete(kUartPort);

    bool callbackWasPending = disconnectCallbackPending_.exchange(false);
    if (callbackWasPending) {
        std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);

        bool disconnectCallbackIsRegistered = (connectionLostCallback_ != nullptr);
        if (disconnectCallbackIsRegistered) {
            connectionLostCallback_();
        }
    }
}

} // namespace gui::esp32
