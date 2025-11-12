#pragma once

#include "gui_common/backends/connection_backend.h"

#include <atomic>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <driver/gpio.h>

namespace gui::esp32
{

using gui::common::PortInfo;

class UartBackend : public gui::common::ConnectionBackend
{
public:
    UartBackend() = default;
    ~UartBackend() override;

    bool initialize();

    std::vector<PortInfo> refreshPorts() override;
    bool connect(const std::string &portIdentifier) override;
    void disconnect() override;
    bool isConnected() const override;

    void setDataCallback(DataCallback dataReceivedCallback) override;
    void setDisconnectCallback(DisconnectCallback connectionLostCallback) override;

    bool write(const std::uint8_t *data, std::size_t size) override;

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
