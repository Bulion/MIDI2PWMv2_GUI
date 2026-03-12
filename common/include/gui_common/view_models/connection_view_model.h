#pragma once

#include "etl/delegate.h"
#include "gui_common/backends/connection_backend.h"
#include "gui_common/message_processor.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"
#include "libcomm/stream_processor.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace gui::common
{

class ConnectionViewModel
{
public:
    enum class PeerState
    {
        Unknown,
        Alive
    };

    using PortsChangedCallback = etl::delegate<void(const std::vector<PortInfo> &availablePortsList)>;
    using ConnectionChangedCallback = etl::delegate<void(bool isNowConnected)>;
    using RawMidiMessageCallback = etl::delegate<void(const midi2pwm::midi::ChannelMessageT &)>;

    ConnectionViewModel(ConnectionBackend &connectionBackend,
                        MidiMessageViewModel &midiMessageViewModel,
                        ChannelTelemetryViewModel &channelTelemetryViewModel);

    std::vector<PortInfo> refreshPorts();
    bool connect(const std::string &portPath);
    void disconnect();

    bool isConnected() const;
    bool isPeerAlive() const;
    PeerState peerState() const;
    const std::vector<PortInfo> &ports() const;
    uint32_t stateVersion() const { return stateVersion_.load(std::memory_order_acquire); }

    void setPortsChangedCallback(PortsChangedCallback portsListChangedCallback);
    void setConnectionChangedCallback(ConnectionChangedCallback connectionStateChangedCallback);
    void setRawMidiMessageCallback(RawMidiMessageCallback rawMidiCallback);

    bool sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config);
    MessageProcessor &messageProcessor();
    void update();

private:
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;
    static constexpr uint32_t PEER_TIMEOUT_MS = 1500;

    bool writeToBackend(const std::uint8_t *data, std::size_t size);
    void handleIncomingParsedFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes);
    void handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes);
    void handleBackendDisconnected();
    void handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage);
    void handlePwmTelemetryReceived(const midi2pwm::pwm::ChannelTelemetry &telemetry);
    void handleChannelConfigReceived(const midi2pwm::pwm::ChannelConfig &config);
    void handleHeartBeatReceived(const midi2pwm::pwm::HeartBeat &heartbeat);
    void handleResponseReceived(const midi2pwm::pwm::Response &response);
    uint32_t getTimeMs() const;

    ConnectionBackend &connectionBackendReference_;
    MidiMessageViewModel &midiMessageViewModelReference_;
    ChannelTelemetryViewModel &channelTelemetryViewModelReference_;
    MessageProcessor messageProcessor_;
    libcomm::StreamProcessor streamProcessor_;

    PortsChangedCallback portsListChangedCallback_;
    ConnectionChangedCallback connectionStateChangedCallback_;
    RawMidiMessageCallback rawMidiMessageCallback_;

    mutable std::mutex viewModelStateMutex_;
    std::mutex streamProcessorMutex_;
    std::vector<PortInfo> cachedAvailablePorts_;
    bool isCurrentlyConnected_{false};
    PeerState peerState_{PeerState::Unknown};
    uint16_t localEpoch_{0};
    uint16_t peerEpoch_{0};
    bool peerEpochKnown_{false};
    uint32_t lastFrameReceivedMs_{0};
    uint32_t lastHeartbeatSentMs_{0};
    std::atomic<bool> pendingTelemetryRequest_{false};
    std::atomic<uint32_t> stateVersion_{0};
};

} // namespace gui::common
