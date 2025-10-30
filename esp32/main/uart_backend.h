#pragma once

#include "gui_common/backends/connection_backend.h"

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

namespace gui::esp32
{

class UartBackend : public gui::common::ConnectionBackend
{
public:
    UartBackend();
    ~UartBackend() override;

    std::vector<std::string> refreshPorts() override;
    bool connect(const std::string &portIdentifier) override;
    void disconnect() override;
    bool isConnected() const override;

    void setDataCallback(DataCallback dataReceivedCallback) override;
    void setDisconnectCallback(DisconnectCallback connectionLostCallback) override;

private:
    static constexpr uart_port_t kUartPort = UART_NUM_0;
    static constexpr gpio_num_t kTxPin = GPIO_NUM_1;
    static constexpr gpio_num_t kRxPin = GPIO_NUM_3;

    static void FreeRtosTaskEntryPoint(void *taskParameterPointer);
    void uartReaderTaskLoop();
    bool startFreeRtosReaderTask();
    void stopFreeRtosReaderTask();

    std::atomic<bool> readerTaskIsRunning_{false};
    std::atomic<bool> disconnectCallbackPending_{false};
    TaskHandle_t readerTaskHandle_{nullptr};

    mutable std::mutex callbackAccessMutex_;
    DataCallback dataReceivedCallback_;
    DisconnectCallback connectionLostCallback_;
};

} // namespace gui::esp32
