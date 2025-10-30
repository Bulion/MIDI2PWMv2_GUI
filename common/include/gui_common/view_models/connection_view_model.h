#pragma once

#include "etl/delegate.h"
#include "gui_common/backends/connection_backend.h"
#include "gui_common/frame_parser.h"
#include "gui_common/message_processor.h"
#include "gui_common/view_models/channel_telemetry_view_model.h"
#include "gui_common/view_models/midi_message_view_model.h"

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

private:
    static constexpr std::size_t EXPECTED_CHANNEL_COUNT = 16;

    bool writeToBackend(const std::uint8_t *data, std::size_t size);
    void handleIncomingParsedFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes);
    void handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes);
    void handleBackendDisconnected();
    void handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage);
    void handlePwmTelemetryReceived(const midi2pwm::pwm::ChannelTelemetry &telemetry);
    void handleHeartBeatReceived(const midi2pwm::pwm::HeartBeat &heartbeat);
    void handleResponseReceived(const midi2pwm::pwm::Response &response);

    ConnectionBackend &connectionBackendReference_;
    MidiMessageViewModel &midiMessageViewModelReference_;
    ChannelTelemetryViewModel &channelTelemetryViewModelReference_;
    MessageProcessor messageProcessor_;
    FrameParser frameParser_;

    PortsChangedCallback portsListChangedCallback_;
    ConnectionChangedCallback connectionStateChangedCallback_;
    RawMidiMessageCallback rawMidiMessageCallback_;

    mutable std::mutex viewModelStateMutex_;
    std::vector<PortInfo> cachedAvailablePorts_;
    bool isCurrentlyConnected_{false};
    ConnectionState connectionState_{ConnectionState::Disconnected};
    std::size_t receivedTelemetryChannelCount_{0};
};

} // namespace gui::common
