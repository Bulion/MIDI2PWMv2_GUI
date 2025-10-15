#pragma once

#include "gui_common/backends/connection_backend.h"
#include "gui_common/frame_parser.h"
#include "gui_common/midi_stream_processor.h"
#include "gui_common/view_models/midi_message_view_model.h"

#include <mutex>
#include <vector>

#include "etl/delegate.h"

namespace gui::common
{

class ConnectionViewModel
{
public:
    using PortsChangedCallback = etl::delegate<void(const std::vector<std::string> &availablePortsList)>;
    using ConnectionChangedCallback = etl::delegate<void(bool isNowConnected)>;

    ConnectionViewModel(ConnectionBackend &connectionBackend, MidiMessageViewModel &midiMessageViewModel);

    std::vector<std::string> refreshPorts();
    bool connect(const std::string &portIdentifier);
    void disconnect();

    bool isConnected() const;
    const std::vector<std::string> &ports() const;

    void setPortsChangedCallback(PortsChangedCallback portsListChangedCallback);
    void setConnectionChangedCallback(ConnectionChangedCallback connectionStateChangedCallback);

private:
    void handleIncomingParsedFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes);
    void handleRawDataFromBackend(const std::uint8_t *receivedData, std::size_t receivedSizeBytes);
    void handleBackendDisconnected();
    void handleMidiChannelMessageReceived(const midi2pwm::midi::ChannelMessageT &midiChannelMessage);

    ConnectionBackend &connectionBackendReference_;
    MidiMessageViewModel &midiMessageViewModelReference_;
    MidiStreamProcessor midiStreamProcessor_;
    FrameParser frameParser_;

    PortsChangedCallback portsListChangedCallback_;
    ConnectionChangedCallback connectionStateChangedCallback_;

    mutable std::mutex viewModelStateMutex_;
    std::vector<std::string> cachedAvailablePorts_;
    bool isCurrentlyConnected_{false};
};

} // namespace gui::common
