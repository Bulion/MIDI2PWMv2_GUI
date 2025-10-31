#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/log.h"

namespace gui::common
{

ConnectionViewModel::ConnectionViewModel(ConnectionBackend &connectionBackend,
                                         MidiMessageViewModel &midiMessageViewModel,
                                         ChannelTelemetryViewModel &channelTelemetryViewModel)
    : connectionBackendReference_(connectionBackend)
    , midiMessageViewModelReference_(midiMessageViewModel)
    , channelTelemetryViewModelReference_(channelTelemetryViewModel)
    , messageProcessor_(MessageProcessor::WriteCallback::create<ConnectionViewModel, &ConnectionViewModel::writeToBackend>(*this))
{
    auto rawDataReceivedCallback = [this](const std::uint8_t *receivedData, std::size_t receivedSizeBytes) {
        handleRawDataFromBackend(receivedData, receivedSizeBytes);
    };
    connectionBackendReference_.setDataCallback(rawDataReceivedCallback);

    auto backendDisconnectedCallback = [this]() {
        handleBackendDisconnected();
    };
    connectionBackendReference_.setDisconnectCallback(backendDisconnectedCallback);

    auto parsedFrameReadyCallback =
        libcomm::StreamProcessor::FrameCallback::create<ConnectionViewModel, &ConnectionViewModel::handleIncomingParsedFrame>(*this);
    streamProcessor_.setFrameCallback(parsedFrameReadyCallback);

    auto midiChannelMessageCallback = MessageProcessor::MidiChannelMessageCallback::
        create<ConnectionViewModel, &ConnectionViewModel::handleMidiChannelMessageReceived>(*this);
    messageProcessor_.setMidiChannelMessageCallback(midiChannelMessageCallback);

    auto pwmTelemetryCallback = MessageProcessor::PwmTelemetryCallback::
        create<ConnectionViewModel, &ConnectionViewModel::handlePwmTelemetryReceived>(*this);
    messageProcessor_.setPwmTelemetryCallback(pwmTelemetryCallback);

    auto heartBeatCallback = MessageProcessor::HeartBeatCallback::
        create<ConnectionViewModel, &ConnectionViewModel::handleHeartBeatReceived>(*this);
    messageProcessor_.setHeartBeatCallback(heartBeatCallback);

    auto responseCallback = MessageProcessor::ResponseCallback::
        create<ConnectionViewModel, &ConnectionViewModel::handleResponseReceived>(*this);
    messageProcessor_.setResponseCallback(responseCallback);
}

bool ConnectionViewModel::writeToBackend(const std::uint8_t *data, std::size_t size)
{
    return connectionBackendReference_.write(data, size);
}

std::vector<PortInfo> ConnectionViewModel::refreshPorts()
{
    std::vector<PortInfo> freshlyEnumeratedPorts = connectionBackendReference_.refreshPorts();

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
        GUI_LOG_ERROR("ConnectionVM", "Cannot connect: port identifier is empty");
        return false;
    }

    bool backendConnectionSucceeded = connectionBackendReference_.connect(portIdentifier);
    if (!backendConnectionSucceeded) {
        GUI_LOG_ERROR("ConnectionVM", "Backend connection failed");
        return false;
    }

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        isCurrentlyConnected_ = true;
        connectionState_ = ConnectionState::WaitingForHeartBeatResponse;
        receivedTelemetryChannelCount_ = 0;
    }

    GUI_LOG_INFO("ConnectionVM", "Backend connected, sending HeartBeat with forced telemetry request");
    constexpr bool REQUEST_TELEMETRY = true;
    bool heartBeatSent = messageProcessor_.sendHeartBeat(REQUEST_TELEMETRY);

    if (!heartBeatSent) {
        GUI_LOG_ERROR("ConnectionVM", "Failed to send initial HeartBeat - disconnecting");
        disconnect();
        return false;
    }

    GUI_LOG_INFO("ConnectionVM", "Initial HeartBeat sent, waiting for response and telemetry data");
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

const std::vector<PortInfo> &ConnectionViewModel::ports() const
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

void ConnectionViewModel::handleIncomingParsedFrame(
    const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes)
{
    GUI_LOG_VERBOSE("ConnectionVM", "Parsed frame received: size=%zu bytes", framePayloadSizeBytes);
    messageProcessor_.handleFrame(framePayloadData, framePayloadSizeBytes);
}

void ConnectionViewModel::handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    streamProcessor_.feed(receivedData, receivedSizeBytes);
}

void ConnectionViewModel::handleBackendDisconnected()
{
    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        isCurrentlyConnected_ = false;
        connectionState_ = ConnectionState::Disconnected;
        receivedTelemetryChannelCount_ = 0;
        streamProcessor_.reset();
    }

    midiMessageViewModelReference_.clear();
    channelTelemetryViewModelReference_.clear();

    bool connectionStateCallbackIsRegistered = connectionStateChangedCallback_.is_valid();
    if (connectionStateCallbackIsRegistered) {
        constexpr bool NOW_DISCONNECTED = false;
        connectionStateChangedCallback_(NOW_DISCONNECTED);
    }
}

void ConnectionViewModel::setRawMidiMessageCallback(RawMidiMessageCallback rawMidiCallback)
{
    rawMidiMessageCallback_ = rawMidiCallback;
}

void ConnectionViewModel::handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage)
{
    GUI_LOG_VERBOSE("ConnectionVM", "MIDI channel message received: type=%d, channel=%d, data1=%u, data2=%u",
                    static_cast<int>(midiChannelMessage.message_type),
                    midiChannelMessage.channel,
                    midiChannelMessage.data1,
                    midiChannelMessage.data2);

    bool rawCallbackIsRegistered = rawMidiMessageCallback_.is_valid();
    if (rawCallbackIsRegistered) {
        GUI_LOG_VERBOSE("ConnectionVM", "Invoking raw MIDI callback");
        rawMidiMessageCallback_(midiChannelMessage);
    } else {
        GUI_LOG_WARNING("ConnectionVM", "Raw MIDI callback not registered - message will not reach assignment logic");
    }

    midiMessageViewModelReference_.updateFromChannelMessage(midiChannelMessage);
}

void ConnectionViewModel::handlePwmTelemetryReceived(const midi2pwm::pwm::ChannelTelemetry &telemetry)
{
    GUI_LOG_VERBOSE("ConnectionVM", "Telemetry received for channel %u", telemetry.channel_number());

    channelTelemetryViewModelReference_.updateFromTelemetry(telemetry);

    bool shouldCheckConnectionState = false;
    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);

        const char *stateNames[] = {"Disconnected", "WaitingForHeartBeatResponse", "WaitingForTelemetryData", "FullyConnected"};
        GUI_LOG_VERBOSE("ConnectionVM", "Current state: %s", stateNames[static_cast<int>(connectionState_)]);

        if (connectionState_ == ConnectionState::WaitingForTelemetryData) {
            receivedTelemetryChannelCount_++;
            GUI_LOG_DEBUG("ConnectionVM", "Received telemetry for channel %u (%zu/%zu)",
                          telemetry.channel_number(), receivedTelemetryChannelCount_, EXPECTED_CHANNEL_COUNT);

            if (receivedTelemetryChannelCount_ >= EXPECTED_CHANNEL_COUNT) {
                shouldCheckConnectionState = true;
            }
        } else if (connectionState_ == ConnectionState::FullyConnected) {
            GUI_LOG_VERBOSE("ConnectionVM", "Ongoing telemetry update for channel %u", telemetry.channel_number());
        } else {
            GUI_LOG_WARNING("ConnectionVM", "Received telemetry in unexpected state: %s",
                            stateNames[static_cast<int>(connectionState_)]);
        }
    }

    if (shouldCheckConnectionState) {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        connectionState_ = ConnectionState::FullyConnected;
        GUI_LOG_INFO("ConnectionVM", "All telemetry data received - connection fully established");

        bool connectionStateCallbackIsRegistered = connectionStateChangedCallback_.is_valid();
        if (connectionStateCallbackIsRegistered) {
            constexpr bool NOW_CONNECTED = true;
            connectionStateChangedCallback_(NOW_CONNECTED);
        }
    }
}

void ConnectionViewModel::handleHeartBeatReceived(const midi2pwm::pwm::HeartBeat &heartbeat)
{
    GUI_LOG_INFO("ConnectionVM", "HeartBeat response received: request_telemetry=%d", heartbeat.request_telemetry());

    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);

    if (connectionState_ == ConnectionState::WaitingForHeartBeatResponse) {
        GUI_LOG_INFO("ConnectionVM", "HeartBeat ACK received, transitioning to WaitingForTelemetryData state");
        connectionState_ = ConnectionState::WaitingForTelemetryData;
        receivedTelemetryChannelCount_ = 0;
    }
}

void ConnectionViewModel::handleResponseReceived(const midi2pwm::pwm::Response &response)
{
    const char *statusString = (response.status() == midi2pwm::pwm::ResponseStatus::ACK) ? "ACK" : "NACK";

    GUI_LOG_INFO("ConnectionVM", "Command Response received: status=%s, error_code=%u",
                 statusString, response.error_code());

    if (response.status() == midi2pwm::pwm::ResponseStatus::NACK) {
        GUI_LOG_ERROR("ConnectionVM", "Device rejected command with error_code=%u", response.error_code());
    }

    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);

    const char *stateNames[] = {"Disconnected", "WaitingForHeartBeatResponse", "WaitingForTelemetryData", "FullyConnected"};
    GUI_LOG_INFO("ConnectionVM", "Response handler - Current connection state: %s", stateNames[static_cast<int>(connectionState_)]);

    if (connectionState_ == ConnectionState::WaitingForHeartBeatResponse) {
        GUI_LOG_INFO("ConnectionVM", "Initial HeartBeat acknowledged, transitioning to WaitingForTelemetryData state");
        connectionState_ = ConnectionState::WaitingForTelemetryData;
        receivedTelemetryChannelCount_ = 0;
    } else {
        GUI_LOG_WARNING("ConnectionVM", "Received Response but not in WaitingForHeartBeatResponse state (current: %s)",
                        stateNames[static_cast<int>(connectionState_)]);
    }
}

bool ConnectionViewModel::isFullyConnected() const
{
    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
    return connectionState_ == ConnectionState::FullyConnected;
}

bool ConnectionViewModel::sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config)
{
    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);

    if (!isCurrentlyConnected_) {
        return false;
    }

    return messageProcessor_.sendChannelConfig(config);
}

} // namespace gui::common
