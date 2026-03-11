#include "gui_common/view_models/connection_view_model.h"
#include "gui_common/log.h"

#include <chrono>
#include <inttypes.h>

namespace gui::common
{

ConnectionViewModel::ConnectionViewModel(ConnectionBackend &connectionBackend,
                                         MidiMessageViewModel &midiMessageViewModel,
                                         ChannelTelemetryViewModel &channelTelemetryViewModel)
    : connectionBackendReference_(connectionBackend)
    , midiMessageViewModelReference_(midiMessageViewModel)
    , channelTelemetryViewModelReference_(channelTelemetryViewModel)
    , messageProcessor_(MessageProcessor::WriteCallback::create<ConnectionViewModel, &ConnectionViewModel::writeToBackend>(*this))
    , localEpoch_(static_cast<uint16_t>(getTimeMs() & 0xFFFF))
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

    auto channelConfigCallback = MessageProcessor::ChannelConfigCallback::
        create<ConnectionViewModel, &ConnectionViewModel::handleChannelConfigReceived>(*this);
    messageProcessor_.setChannelConfigCallback(channelConfigCallback);

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

    if (portsListChangedCallback_.is_valid()) {
        portsListChangedCallback_(freshlyEnumeratedPorts);
    }

    return freshlyEnumeratedPorts;
}

bool ConnectionViewModel::connect(const std::string &portIdentifier)
{
    if (portIdentifier.empty()) {
        GUI_LOG_ERROR("ConnectionVM", "Cannot connect: port identifier is empty");
        return false;
    }

    {
        std::lock_guard<std::mutex> streamLock(streamProcessorMutex_);
        streamProcessor_.reset();
    }

    bool backendConnectionSucceeded = connectionBackendReference_.connect(portIdentifier);
    if (!backendConnectionSucceeded) {
        GUI_LOG_ERROR("ConnectionVM", "Backend connection failed");
        return false;
    }

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        isCurrentlyConnected_ = true;
        peerState_ = PeerState::Unknown;
        peerEpochKnown_ = false;
        lastFrameReceivedMs_ = getTimeMs();
        lastHeartbeatSentMs_ = getTimeMs();
    }

    GUI_LOG_INFO("ConnectionVM", "Backend connected, epoch=%u", localEpoch_);
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

bool ConnectionViewModel::isPeerAlive() const
{
    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
    return peerState_ == PeerState::Alive;
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

void ConnectionViewModel::setRawMidiMessageCallback(RawMidiMessageCallback rawMidiCallback)
{
    rawMidiMessageCallback_ = rawMidiCallback;
}

void ConnectionViewModel::handleIncomingParsedFrame(
    const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes)
{
    GUI_LOG_VERBOSE("ConnectionVM", "Parsed frame received: size=%zu bytes", framePayloadSizeBytes);
    messageProcessor_.handleFrame(framePayloadData, framePayloadSizeBytes);
}

void ConnectionViewModel::handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    std::lock_guard<std::mutex> streamLock(streamProcessorMutex_);
    streamProcessor_.feed(receivedData, receivedSizeBytes);
}

void ConnectionViewModel::handleBackendDisconnected()
{
    {
        std::lock_guard<std::mutex> streamLock(streamProcessorMutex_);
        streamProcessor_.reset();
    }

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        isCurrentlyConnected_ = false;
        peerState_ = PeerState::Unknown;
        peerEpochKnown_ = false;
    }

    midiMessageViewModelReference_.clear();
    channelTelemetryViewModelReference_.clear();

    if (connectionStateChangedCallback_.is_valid()) {
        connectionStateChangedCallback_(false);
    }
}

void ConnectionViewModel::handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage)
{
    GUI_LOG_VERBOSE("ConnectionVM", "MIDI channel message received: type=%d, channel=%d, data1=%u, data2=%u",
                    static_cast<int>(midiChannelMessage.message_type),
                    midiChannelMessage.channel,
                    midiChannelMessage.data1,
                    midiChannelMessage.data2);

    if (rawMidiMessageCallback_.is_valid()) {
        rawMidiMessageCallback_(midiChannelMessage);
    }

    midiMessageViewModelReference_.updateFromChannelMessage(midiChannelMessage);

    bool shouldNotifyConnected = false;
    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        lastFrameReceivedMs_ = getTimeMs();
        if (peerState_ == PeerState::Unknown) {
            peerState_ = PeerState::Alive;
            shouldNotifyConnected = true;
        }
    }

    if (shouldNotifyConnected && connectionStateChangedCallback_.is_valid()) {
        connectionStateChangedCallback_(true);
    }
}

void ConnectionViewModel::handlePwmTelemetryReceived(const midi2pwm::pwm::ChannelTelemetry &telemetry)
{
    GUI_LOG_VERBOSE("ConnectionVM", "Telemetry received for channel %u", telemetry.channel_number());

    channelTelemetryViewModelReference_.updateFromTelemetry(telemetry);

    bool shouldNotifyConnected = false;
    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        lastFrameReceivedMs_ = getTimeMs();
        if (peerState_ == PeerState::Unknown) {
            peerState_ = PeerState::Alive;
            shouldNotifyConnected = true;
        }
    }

    if (shouldNotifyConnected && connectionStateChangedCallback_.is_valid()) {
        connectionStateChangedCallback_(true);
    }
}

void ConnectionViewModel::handleChannelConfigReceived(const midi2pwm::pwm::ChannelConfig &config)
{
    GUI_LOG_INFO("ConnectionVM", "ChannelConfig received for channel %u", config.channel_number());

    channelTelemetryViewModelReference_.updateFromConfig(config);

    bool shouldNotifyConnected = false;
    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        lastFrameReceivedMs_ = getTimeMs();
        if (peerState_ == PeerState::Unknown) {
            peerState_ = PeerState::Alive;
            shouldNotifyConnected = true;
        }
    }

    if (shouldNotifyConnected && connectionStateChangedCallback_.is_valid()) {
        connectionStateChangedCallback_(true);
    }
}

void ConnectionViewModel::handleHeartBeatReceived(const midi2pwm::pwm::HeartBeat &heartbeat)
{
    uint16_t receivedEpoch = heartbeat.epoch();
    GUI_LOG_DEBUG("ConnectionVM", "HeartBeat received: epoch=%u, request_telemetry=%d",
                  receivedEpoch, heartbeat.request_telemetry());

    bool shouldNotifyConnected = false;
    bool shouldClearTelemetry = false;

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
        lastFrameReceivedMs_ = getTimeMs();

        bool epochChanged = peerEpochKnown_ && (receivedEpoch != peerEpoch_);
        bool firstContact = !peerEpochKnown_;

        peerEpoch_ = receivedEpoch;
        peerEpochKnown_ = true;

        if (peerState_ == PeerState::Unknown) {
            peerState_ = PeerState::Alive;
            shouldNotifyConnected = true;
        }

        if (firstContact || epochChanged) {
            if (epochChanged) {
                GUI_LOG_INFO("ConnectionVM", "Peer epoch changed %u -> %u, requesting full telemetry resync",
                             peerEpoch_, receivedEpoch);
            }
            pendingTelemetryRequest_.store(true);
            shouldClearTelemetry = true;
        }
    }

    if (shouldClearTelemetry) {
        channelTelemetryViewModelReference_.clear();
    }

    if (shouldNotifyConnected && connectionStateChangedCallback_.is_valid()) {
        connectionStateChangedCallback_(true);
    }
}

void ConnectionViewModel::handleResponseReceived(const midi2pwm::pwm::Response &response)
{
    const char *statusString = (response.status() == midi2pwm::pwm::ResponseStatus::ACK) ? "ACK" : "NACK";
    GUI_LOG_INFO("ConnectionVM", "Response received: status=%s, error_code=%" PRIu32,
                 statusString, response.error_code());

    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);
    lastFrameReceivedMs_ = getTimeMs();
}

bool ConnectionViewModel::sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config)
{
    std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);

    if (!isCurrentlyConnected_) {
        return false;
    }

    return messageProcessor_.sendChannelConfig(config);
}

MessageProcessor &ConnectionViewModel::messageProcessor()
{
    return messageProcessor_;
}

uint32_t ConnectionViewModel::getTimeMs() const
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

void ConnectionViewModel::update()
{
    bool shouldSendHeartbeat = false;
    bool shouldNotifyDisconnected = false;

    {
        std::lock_guard<std::mutex> stateLock(viewModelStateMutex_);

        if (!isCurrentlyConnected_) {
            return;
        }

        uint32_t currentTimeMs = getTimeMs();

        if (peerState_ == PeerState::Alive && (currentTimeMs - lastFrameReceivedMs_) > PEER_TIMEOUT_MS) {
            GUI_LOG_INFO("ConnectionVM", "Peer timeout (no frames for >%" PRIu32 " ms), transitioning to PeerUnknown",
                         PEER_TIMEOUT_MS);
            peerState_ = PeerState::Unknown;
            peerEpochKnown_ = false;
            shouldNotifyDisconnected = true;
        }

        if ((currentTimeMs - lastHeartbeatSentMs_) >= HEARTBEAT_INTERVAL_MS) {
            shouldSendHeartbeat = true;
            lastHeartbeatSentMs_ = currentTimeMs;
        }
    }

    if (shouldNotifyDisconnected && connectionStateChangedCallback_.is_valid()) {
        connectionStateChangedCallback_(false);
    }

    if (pendingTelemetryRequest_.exchange(false)) {
        messageProcessor_.sendHeartBeat(true, localEpoch_);
    } else if (shouldSendHeartbeat) {
        bool sent = messageProcessor_.sendHeartBeat(false, localEpoch_);
        if (!sent) {
            GUI_LOG_ERROR("ConnectionVM", "Failed to send HeartBeat");
        }
    }
}

} // namespace gui::common
