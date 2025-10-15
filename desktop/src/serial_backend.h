#pragma once

#include "file_descriptor_raii.h"
#include "gui_common/backends/connection_backend.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace gui::desktop
{

class SerialBackend : public gui::common::ConnectionBackend
{
public:
    SerialBackend();
    ~SerialBackend() override;

    std::vector<std::string> refreshPorts() override;
    bool connect(const std::string &serialPortDevicePath) override;
    void disconnect() override;
    bool isConnected() const override;

    void setDataCallback(DataCallback dataReceivedCallback) override;
    void setDisconnectCallback(DisconnectCallback connectionLostCallback) override;

private:
    bool configureSerialPortTermios(int fileDescriptor);
    void serialPortReaderThreadLoop();

    std::atomic<bool> readerThreadIsRunning_{false};
    std::atomic<bool> disconnectCallbackPending_{false};
    FileDescriptorRAII serialPortFileDescriptor_;

    DataCallback dataReceivedCallback_;
    DisconnectCallback connectionLostCallback_;

    std::mutex callbackAccessMutex_;
    std::thread readerThreadHandle_;
    std::mutex connectionStateMutex_;
};

} // namespace gui::desktop
