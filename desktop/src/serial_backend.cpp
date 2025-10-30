#include "serial_backend.h"

#include "CSerialPort/SerialPort.h"
#include "CSerialPort/SerialPortInfo.h"
#include "etl/array.h"
#include "gui_common/log.h"

#include <chrono>
#include <thread>

namespace gui::desktop
{

constexpr int kPreferredBaudRate = 921600;
constexpr int kReadTimeoutMilliseconds = 100;

namespace
{

std::string getFriendlyDeviceName(const std::string &hardwareId, const std::string &defaultDescription)
{
    if (hardwareId == "cafe:4002" || hardwareId == "cafe:4004") {
        return "MIDI2PWM Device";
    }

    return defaultDescription;
}

} // anonymous namespace

SerialBackend::SerialBackend() = default;

SerialBackend::~SerialBackend()
{
    disconnect();
}

std::vector<gui::common::PortInfo> SerialBackend::refreshPorts()
{
    std::vector<gui::common::PortInfo> availablePortInfos;

    try {
        std::vector<itas109::SerialPortInfo> detectedPorts = itas109::CSerialPortInfo::availablePortInfos();

        for (const auto &portInfo : detectedPorts) {
            std::string portName = portInfo.portName;

#ifdef __linux__
            bool isUsbCdcDevice =
                (portName.find("ttyACM") != std::string::npos) || (portName.find("ttyUSB") != std::string::npos);
            if (!isUsbCdcDevice) {
                continue;
            }
#endif

            gui::common::PortInfo info;
            info.portPath = portName;
            info.hardwareId = portInfo.hardwareId;

            std::string description = getFriendlyDeviceName(portInfo.hardwareId, portInfo.description);
            if (description.empty() || description == "n/a") {
                info.friendlyName = portName;
            } else {
                info.friendlyName = std::string(description) + " (" + portName + ")";
            }

            availablePortInfos.push_back(info);
        }
    } catch (const std::exception &exception) {
        (void)exception;
    }

    std::sort(
        availablePortInfos.begin(),
        availablePortInfos.end(),
        [](const gui::common::PortInfo &a, const gui::common::PortInfo &b) {
            return a.portPath < b.portPath;
        });

    return availablePortInfos;
}

bool SerialBackend::connect(const std::string &serialPortDevicePath)
{
    std::lock_guard<std::mutex> connectionLock(connectionStateMutex_);

    bool alreadyConnected = readerThreadIsRunning_;
    if (alreadyConnected) {
        GUI_LOG_DEBUG("SerialBackend", "Already connected, disconnecting first");
        disconnect();
    }

    try {
        GUI_LOG_DEBUG("SerialBackend", "Connecting to port: %s", serialPortDevicePath.c_str());
        serialPort_ = std::make_unique<itas109::CSerialPort>();
        serialPort_->init(serialPortDevicePath.c_str(), kPreferredBaudRate);

        bool openSucceeded = serialPort_->open();
        if (!openSucceeded) {
            GUI_LOG_ERROR(
                "SerialBackend",
                "Failed to open port: %s (error: %d)",
                serialPortDevicePath.c_str(),
                serialPort_->getLastError());
            serialPort_.reset();
            return false;
        }

        GUI_LOG_INFO("SerialBackend", "Connected to %s at %d baud", serialPortDevicePath.c_str(), kPreferredBaudRate);

        readerThreadIsRunning_ = true;
        disconnectCallbackPending_ = true;

        readerThreadHandle_ = std::thread(&SerialBackend::serialPortReaderThreadLoop, this);
        return true;
    } catch (const std::exception &exception) {
        GUI_LOG_ERROR("SerialBackend", "Exception opening port: %s", exception.what());
        serialPort_.reset();
        return false;
    }
}

void SerialBackend::disconnect()
{
    std::thread readerThreadToJoin;

    {
        std::lock_guard<std::mutex> connectionLock(connectionStateMutex_);

        bool notConnected = !readerThreadIsRunning_ && !serialPort_;
        if (notConnected) {
            return;
        }

        readerThreadIsRunning_ = false;

        if (serialPort_ && serialPort_->isOpen()) {
            try {
                serialPort_->close();
            } catch (const std::exception &exception) {
                (void)exception;
            }
        }

        readerThreadToJoin = std::move(readerThreadHandle_);
    }

    bool threadNeedsToBeJoined = readerThreadToJoin.joinable();
    if (threadNeedsToBeJoined) {
        readerThreadToJoin.join();
    }

    bool callbackWasPending = disconnectCallbackPending_.exchange(false);
    if (callbackWasPending) {
        std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);

        bool disconnectCallbackIsRegistered = (connectionLostCallback_ != nullptr);
        if (disconnectCallbackIsRegistered) {
            connectionLostCallback_();
        }
    }
}

bool SerialBackend::isConnected() const
{
    return readerThreadIsRunning_.load();
}

void SerialBackend::setDataCallback(DataCallback dataReceivedCallback)
{
    std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
    dataReceivedCallback_ = dataReceivedCallback;
}

void SerialBackend::setDisconnectCallback(DisconnectCallback connectionLostCallback)
{
    std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
    connectionLostCallback_ = connectionLostCallback;
}

bool SerialBackend::write(const std::uint8_t *data, std::size_t size)
{
    GUI_LOG_INFO("SerialBackend", "write() called with %zu bytes", size);

    bool shouldTriggerDisconnect = false;

    {
        std::lock_guard<std::mutex> connectionLock(connectionStateMutex_);

        bool notConnected = !serialPort_ || !serialPort_->isOpen();
        if (notConnected) {
            GUI_LOG_ERROR("SerialBackend", "Cannot write: not connected");
            return false;
        }

        GUI_LOG_DEBUG("SerialBackend", "Connection OK, attempting writeData()");

        try {
            int bytesWritten = serialPort_->writeData(reinterpret_cast<const char *>(data), static_cast<int>(size));

            if (bytesWritten < 0) {
                int errorCode = serialPort_->getLastError();
                const char *errorMessage = serialPort_->getLastErrorMsg();
                GUI_LOG_ERROR("SerialBackend", "Write failed: error code %d (%s)", errorCode, errorMessage);

                bool isDisconnectionError =
                    (errorCode == itas109::ErrorNotOpen || errorCode == itas109::ErrorWriteFailed);
                if (isDisconnectionError) {
                    GUI_LOG_ERROR("SerialBackend", "Device disconnection detected via write error");
                    shouldTriggerDisconnect = true;
                }
                return false;
            }

            bool writeSucceeded = (bytesWritten == static_cast<int>(size));

            if (!writeSucceeded) {
                GUI_LOG_ERROR(
                    "SerialBackend",
                    "Write incomplete: requested %zu bytes, wrote %d (error: %d)",
                    size,
                    bytesWritten,
                    serialPort_->getLastError());
            } else {
                GUI_LOG_INFO("SerialBackend", "Wrote %d bytes successfully", bytesWritten);
            }

            return writeSucceeded;
        } catch (const std::exception &exception) {
            GUI_LOG_ERROR("SerialBackend", "Exception writing data: %s", exception.what());
            return false;
        }
    }

    if (shouldTriggerDisconnect) {
        disconnect();
    }

    return false;
}

void SerialBackend::serialPortReaderThreadLoop()
{
    constexpr size_t READ_BUFFER_SIZE_BYTES = 4096;
    etl::array<char, READ_BUFFER_SIZE_BYTES> receiveBuffer{};

    GUI_LOG_DEBUG("SerialBackend", "Reader thread started");

    int consecutiveZeroReads = 0;
    constexpr int MAX_ZERO_READS_BEFORE_PORT_CHECK = 100;

    while (readerThreadIsRunning_.load()) {
        try {
            if (!serialPort_ || !serialPort_->isOpen()) {
                GUI_LOG_ERROR("SerialBackend", "Port is no longer open");
                break;
            }

            int bytesRead = serialPort_->readAllData(receiveBuffer.data());

            if (bytesRead > 0) {
                consecutiveZeroReads = 0;
                GUI_LOG_VERBOSE("SerialBackend", "Read %d bytes", bytesRead);
                DataCallback localCallbackCopy;
                {
                    std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
                    localCallbackCopy = dataReceivedCallback_;
                }

                bool callbackIsRegistered = (localCallbackCopy != nullptr);
                if (callbackIsRegistered) {
                    localCallbackCopy(
                        reinterpret_cast<const std::uint8_t *>(receiveBuffer.data()), static_cast<size_t>(bytesRead));
                }
            } else if (bytesRead == 0) {
                consecutiveZeroReads++;

                if (consecutiveZeroReads >= MAX_ZERO_READS_BEFORE_PORT_CHECK) {
                    if (!serialPort_->isOpen()) {
                        GUI_LOG_ERROR("SerialBackend", "Port check failed - device disconnected");
                        break;
                    }
                    consecutiveZeroReads = 0;
                }

                constexpr int POLLING_DELAY_MILLISECONDS = 10;
                std::this_thread::sleep_for(std::chrono::milliseconds(POLLING_DELAY_MILLISECONDS));
            } else {
                int errorCode = serialPort_->getLastError();
                const char *errorMessage = serialPort_->getLastErrorMsg();
                GUI_LOG_ERROR(
                    "SerialBackend", "Serial read error: %d, error code: %d (%s)", bytesRead, errorCode, errorMessage);

                bool isDisconnectionError =
                    (errorCode == itas109::ErrorNotOpen || errorCode == itas109::ErrorReadFailed);
                if (isDisconnectionError) {
                    GUI_LOG_ERROR("SerialBackend", "Device disconnection detected via read error");
                }
                break;
            }
        } catch (const std::exception &exception) {
            GUI_LOG_ERROR("SerialBackend", "Serial exception: %s", exception.what());
            break;
        }
    }

    readerThreadIsRunning_ = false;

    bool callbackWasPending = disconnectCallbackPending_.exchange(false);
    if (callbackWasPending) {
        DisconnectCallback localCallbackCopy;
        {
            std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
            localCallbackCopy = connectionLostCallback_;
        }

        bool callbackIsRegistered = (localCallbackCopy != nullptr);
        if (callbackIsRegistered) {
            localCallbackCopy();
        }
    }
}

} // namespace gui::desktop
