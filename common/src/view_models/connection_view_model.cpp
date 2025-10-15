#include "gui_common/view_models/connection_view_model.h"

namespace gui::common
{

ConnectionViewModel::ConnectionViewModel(ConnectionBackend &connectionBackend, MidiMessageViewModel &midiMessageViewModel)
    : connectionBackendReference_(connectionBackend)
    , midiMessageViewModelReference_(midiMessageViewModel)
{
    auto rawDataReceivedCallback = [this](const std::uint8_t *receivedData, std::size_t receivedSizeBytes) {
        handleRawDataFromBackend(receivedData, receivedSizeBytes);
    };
    connectionBackendReference_.setDataCallback(rawDataReceivedCallback);

    auto backendDisconnectedCallback = [this]() {
        handleBackendDisconnected();
    };
    connectionBackendReference_.setDisconnectCallback(backendDisconnectedCallback);

    auto parsedFrameReadyCallback = FrameParser::FrameCallback::create<ConnectionViewModel, &ConnectionViewModel::handleIncomingParsedFrame>(*this);
    frameParser_.setCallback(parsedFrameReadyCallback);

    auto midiChannelMessageCallback = MidiStreamProcessor::ChannelMessageCallback::create<ConnectionViewModel, &ConnectionViewModel::handleMidiChannelMessageReceived>(*this);
    midiStreamProcessor_.setChannelMessageCallback(midiChannelMessageCallback);
}

std::vector<std::string> ConnectionViewModel::refreshPorts()
{
    std::vector<std::string> freshlyEnumeratedPorts = connectionBackendReference_.refreshPorts();

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        cachedAvailablePorts_ = freshlyEnumeratedPorts;
    }

    bool portsChangedCallbackIsRegistered = portsListChangedCallback_.is_valid();
    if (portsChangedCallbackIsRegistered) {
        portsListChangedCallback_(freshlyEnumeratedPorts);
    }

    return freshlyEnumeratedPorts;
}

bool ConnectionViewModel::connect(const std::string &portIdentifier)
{
    bool identifierIsEmpty = portIdentifier.empty();
    if (identifierIsEmpty) {
        return false;
    }

    bool backendConnectionSucceeded = connectionBackendReference_.connect(portIdentifier);
    if (!backendConnectionSucceeded) {
        return false;
    }

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        isCurrentlyConnected_ = true;
    }

    bool connectionStateCallbackIsRegistered = connectionStateChangedCallback_.is_valid();
    if (connectionStateCallbackIsRegistered) {
        constexpr bool NOW_CONNECTED = true;
        connectionStateChangedCallback_(NOW_CONNECTED);
    }

    return true;
}

void ConnectionViewModel::disconnect()
{
    connectionBackendReference_.disconnect();
}

bool ConnectionViewModel::isConnected() const
{
    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
    return isCurrentlyConnected_;
}

const std::vector<std::string> &ConnectionViewModel::ports() const
{
    return cachedAvailablePorts_;
}

void ConnectionViewModel::setPortsChangedCallback(PortsChangedCallback portsListChangedCallback)
{
    portsListChangedCallback_ = portsListChangedCallback;
}

void ConnectionViewModel::setConnectionChangedCallback(ConnectionChangedCallback connectionStateChangedCallback)
{
    connectionStateChangedCallback_ = connectionStateChangedCallback;
}

void ConnectionViewModel::handleIncomingParsedFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes)
{
    midiStreamProcessor_.handleFrame(framePayloadData, framePayloadSizeBytes);
}

void ConnectionViewModel::handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
    frameParser_.feed(receivedData, receivedSizeBytes);
}

void ConnectionViewModel::handleBackendDisconnected()
{
    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        isCurrentlyConnected_ = false;
        frameParser_.reset();
    }

    midiMessageViewModelReference_.clear();

    bool connectionStateCallbackIsRegistered = connectionStateChangedCallback_.is_valid();
    if (connectionStateCallbackIsRegistered) {
        constexpr bool NOW_DISCONNECTED = false;
        connectionStateChangedCallback_(NOW_DISCONNECTED);
    }
}

void ConnectionViewModel::handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage)
{
    midiMessageViewModelReference_.updateFromChannelMessage(midiChannelMessage);
}

} // namespace gui::common
