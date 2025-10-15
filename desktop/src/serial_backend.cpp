#include "serial_backend.h"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <string>

#include <cctype>

#include "etl/algorithm.h"
#include "etl/array.h"

namespace gui::desktop
{

namespace
{

constexpr const char *kDeviceDirectory = "/dev";

bool containsOnlyDigitsAfterOffset(const std::string &deviceName, std::size_t startOffset)
{
    bool offsetIsOutOfBounds = (startOffset >= deviceName.size());
    if (offsetIsOutOfBounds) {
        return false;
    }

    auto startIterator = deviceName.begin() + static_cast<std::ptrdiff_t>(startOffset);
    auto isCharacterADigit = [](unsigned char character) { return std::isdigit(character) != 0; };

    return etl::all_of(startIterator, deviceName.end(), isCharacterADigit);
}

bool isAllowedSerialDeviceForEnumeration(const std::string &deviceName)
{
    constexpr size_t TTY_PREFIX_START_POSITION = 0;
    constexpr const char *TTY_DEVICE_PREFIX = "tty";
    bool deviceNameStartsWithTty = (deviceName.rfind(TTY_DEVICE_PREFIX, TTY_PREFIX_START_POSITION) == TTY_PREFIX_START_POSITION);

    if (!deviceNameStartsWithTty) {
        return false;
    }

    constexpr const char *LEGACY_SERIAL_PORT_PREFIX = "ttyS";
    constexpr size_t LEGACY_SERIAL_PORT_NUMBER_OFFSET = 4;
    bool isLegacySerialPortWithNumber = (deviceName.rfind(LEGACY_SERIAL_PORT_PREFIX, TTY_PREFIX_START_POSITION) == TTY_PREFIX_START_POSITION)
                                        && containsOnlyDigitsAfterOffset(deviceName, LEGACY_SERIAL_PORT_NUMBER_OFFSET);
    if (isLegacySerialPortWithNumber) {
        return false;
    }

    constexpr size_t GENERIC_TTY_NUMBER_OFFSET = 3;
    bool isGenericTtyWithOnlyNumbers = containsOnlyDigitsAfterOffset(deviceName, GENERIC_TTY_NUMBER_OFFSET);
    if (isGenericTtyWithOnlyNumbers) {
        return false;
    }

    return true;
}

#ifdef B921600
constexpr speed_t kPreferredBaud = B921600;
#else
constexpr speed_t kPreferredBaud = B115200;
#endif

} // namespace

SerialBackend::SerialBackend() = default;

SerialBackend::~SerialBackend()
{
    disconnect();
}

std::vector<std::string> SerialBackend::refreshPorts()
{
    std::vector<std::string> availableSerialPortPaths;

    try {
        for (const auto &deviceFileEntry : std::filesystem::directory_iterator(kDeviceDirectory)) {
            bool entryIsCharacterDevice = deviceFileEntry.is_character_file();
            if (!entryIsCharacterDevice) {
                continue;
            }

            std::string deviceFilename = deviceFileEntry.path().filename().string();
            bool deviceShouldBeIncludedInList = isAllowedSerialDeviceForEnumeration(deviceFilename);
            if (!deviceShouldBeIncludedInList) {
                continue;
            }

            std::string fullDevicePath = deviceFileEntry.path().string();
            availableSerialPortPaths.emplace_back(fullDevicePath);
        }
    } catch (const std::filesystem::filesystem_error &filesystemException) {
        (void)filesystemException;
    }

    std::sort(availableSerialPortPaths.begin(), availableSerialPortPaths.end());
    return availableSerialPortPaths;
}

bool SerialBackend::connect(const std::string &serialPortDevicePath)
{
    std::lock_guard<std::mutex> connectionLock(connectionStateMutex_);

    bool alreadyConnected = readerThreadIsRunning_;
    if (alreadyConnected) {
        disconnect();
    }

    constexpr int OPEN_FOR_READ_WRITE_NO_CONTROLLING_TTY = O_RDWR | O_NOCTTY;
    int openedFileDescriptor = ::open(serialPortDevicePath.c_str(), OPEN_FOR_READ_WRITE_NO_CONTROLLING_TTY);

    bool openFailed = (openedFileDescriptor < 0);
    if (openFailed) {
        return false;
    }

    bool termiosConfigurationSucceeded = configureSerialPortTermios(openedFileDescriptor);
    if (!termiosConfigurationSucceeded) {
        ::close(openedFileDescriptor);
        return false;
    }

    serialPortFileDescriptor_.reset(openedFileDescriptor);
    readerThreadIsRunning_ = true;
    disconnectCallbackPending_ = true;

    readerThreadHandle_ = std::thread(&SerialBackend::serialPortReaderThreadLoop, this);
    return true;
}

void SerialBackend::disconnect()
{
    std::thread readerThreadToJoin;

    {
        std::lock_guard<std::mutex> connectionLock(connectionStateMutex_);

        bool notConnected = !readerThreadIsRunning_ && !serialPortFileDescriptor_.isValid();
        if (notConnected) {
            return;
        }

        readerThreadIsRunning_ = false;
        serialPortFileDescriptor_.reset();

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

bool SerialBackend::configureSerialPortTermios(int fileDescriptor)
{
    struct termios terminalAttributesConfiguration{};

    constexpr int TCGETATTR_SUCCESS = 0;
    bool getCurrentAttributesFailed = (tcgetattr(fileDescriptor, &terminalAttributesConfiguration) != TCGETATTR_SUCCESS);
    if (getCurrentAttributesFailed) {
        return false;
    }

    cfmakeraw(&terminalAttributesConfiguration);

    constexpr unsigned int IGNORE_MODEM_CONTROL_LINES_AND_ENABLE_RECEIVER = CLOCAL | CREAD;
    terminalAttributesConfiguration.c_cflag |= IGNORE_MODEM_CONTROL_LINES_AND_ENABLE_RECEIVER;

    cfsetispeed(&terminalAttributesConfiguration, kPreferredBaud);
    cfsetospeed(&terminalAttributesConfiguration, kPreferredBaud);

    constexpr cc_t READ_TIMEOUT_DECISECONDS = 1;
    constexpr cc_t MINIMUM_BYTES_FOR_READ_TO_RETURN = 0;
    terminalAttributesConfiguration.c_cc[VTIME] = READ_TIMEOUT_DECISECONDS;
    terminalAttributesConfiguration.c_cc[VMIN] = MINIMUM_BYTES_FOR_READ_TO_RETURN;

    constexpr int TCSETATTR_SUCCESS = 0;
    bool setNewAttributesSucceeded = (tcsetattr(fileDescriptor, TCSANOW, &terminalAttributesConfiguration) == TCSETATTR_SUCCESS);

    return setNewAttributesSucceeded;
}

void SerialBackend::serialPortReaderThreadLoop()
{
    constexpr size_t READ_BUFFER_SIZE_BYTES = 512;
    etl::array<std::uint8_t, READ_BUFFER_SIZE_BYTES> receiveBuffer{};

    while (readerThreadIsRunning_.load()) {
        ssize_t bytesReadOrError = ::read(serialPortFileDescriptor_.get(), receiveBuffer.data(), receiveBuffer.size());

        bool dataWasSuccessfullyRead = (bytesReadOrError > 0);
        if (dataWasSuccessfullyRead) {
            DataCallback localCallbackCopy;
            {
                std::lock_guard<std::mutex> callbackLock(callbackAccessMutex_);
                localCallbackCopy = dataReceivedCallback_;
            }

            bool callbackIsRegistered = (localCallbackCopy != nullptr);
            if (callbackIsRegistered) {
                size_t bytesReadCount = static_cast<std::size_t>(bytesReadOrError);
                localCallbackCopy(receiveBuffer.data(), bytesReadCount);
            }
            continue;
        }

        bool endOfFileReached = (bytesReadOrError == 0);
        if (endOfFileReached) {
            break;
        }

        bool readSystemCallInterrupted = (errno == EINTR);
        if (readSystemCallInterrupted) {
            continue;
        }

        bool readWouldBlockOrNoDataAvailable = (errno == EAGAIN) || (errno == EWOULDBLOCK);
        if (readWouldBlockOrNoDataAvailable) {
            constexpr int POLLING_DELAY_MILLISECONDS = 10;
            std::this_thread::sleep_for(std::chrono::milliseconds(POLLING_DELAY_MILLISECONDS));
            continue;
        }

        break;
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
