#pragma once

#include "gui_common/backends/connection_backend.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace itas109
{
class CSerialPort;
}

namespace gui::desktop
{

class SerialBackend : public gui::common::ConnectionBackend
{
public:
    SerialBackend();
    ~SerialBackend() override;

    std::vector<gui::common::PortInfo> refreshPorts() override;
    bool connect(const std::string &serialPortDevicePath) override;
    void disconnect() override;
    bool isConnected() const override;

    void setDataCallback(DataCallback dataReceivedCallback) override;
    void setDisconnectCallback(DisconnectCallback connectionLostCallback) override;

    bool write(const std::uint8_t *data, std::size_t size) override;

private:
    void serialPortReaderThreadLoop();

    std::atomic<bool> readerThreadIsRunning_{false};
    std::atomic<bool> disconnectCallbackPending_{false};
    std::unique_ptr<itas109::CSerialPort> serialPort_;

    DataCallback dataReceivedCallback_;
    DisconnectCallback connectionLostCallback_;

    std::mutex callbackAccessMutex_;
    std::thread readerThreadHandle_;
    std::mutex connectionStateMutex_;
};

} // namespace gui::desktop
