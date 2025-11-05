#pragma once

#include "etl/delegate.h"
#include "gui_common/backends/connection_backend.h"
#include "gui_common/message_processor.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"
#include "libcomm/stream_processor.h"

#include <mutex>
#include <vector>

namespace gui::common
{

class ConnectionViewModel
{
public:
    enum class ConnectionState
    {
        Disconnected,
        WaitingForHeartBeatResponse,
        WaitingForTelemetryData,
        FullyConnected
    };

    enum class HeartBeatState
    {
        Idle,
        Active,
        WaitingForResponse
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
    bool isFullyConnected() const;
    const std::vector<PortInfo> &ports() const;

    void setPortsChangedCallback(PortsChangedCallback portsListChangedCallback);
    void setConnectionChangedCallback(ConnectionChangedCallback connectionStateChangedCallback);
    void setRawMidiMessageCallback(RawMidiMessageCallback rawMidiCallback);

    bool sendChannelConfig(const midi2pwm::pwm::ChannelConfigT &config);
    void update();

private:
    static constexpr std::size_t EXPECTED_CHANNEL_COUNT = 16;
    static constexpr uint32_t TELEMETRY_IDLE_TIMEOUT_MS = 5000;
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 2000;
    static constexpr uint32_t HEARTBEAT_RESPONSE_TIMEOUT_MS = 1000;

    bool writeToBackend(const std::uint8_t *data, std::size_t size);
    void handleIncomingParsedFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes);
    void handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes);
    void handleBackendDisconnected();
    void handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage);
    void handlePwmTelemetryReceived(const midi2pwm::pwm::ChannelTelemetry &telemetry);
    void handleHeartBeatReceived(const midi2pwm::pwm::HeartBeat &heartbeat);
    void handleResponseReceived(const midi2pwm::pwm::Response &response);
    void handleConnectionLost();
    void sendHeartBeat();
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
    std::vector<PortInfo> cachedAvailablePorts_;
    bool isCurrentlyConnected_{false};
    ConnectionState connectionState_{ConnectionState::Disconnected};
    std::size_t receivedTelemetryChannelCount_{0};
    HeartBeatState heartBeatState_{HeartBeatState::Idle};
    uint32_t lastTelemetryReceivedMs_{0};
    uint32_t lastHeartBeatSentMs_{0};
    uint32_t heartBeatResponseDeadlineMs_{0};
};

} // namespace gui::common
