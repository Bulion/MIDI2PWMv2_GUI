#pragma once

#include "gui_common/message_processor.h"
#include "gui_common/view_models/connection_view_model.h"
#include "uart_backend.h"

#include "ota_messages_generated.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace gui::esp32
{

struct OtaDisplayState
{
    std::atomic<uint32_t> version{0};
    std::mutex mutex;
    midi2pwm::ota::OtaStatus status{midi2pwm::ota::OtaStatus::Idle};
    uint16_t chunksReceived{0};
    uint16_t totalChunks{0};
    uint32_t compressedSize{0};
    uint32_t firmwareSize{0};
};

void initOtaHandler(gui::common::ConnectionViewModel &vm, UartBackend &backend);
void registerOtaCallbacks(gui::common::MessageProcessor &msgProc);
void checkOtaDataTimeout();
OtaDisplayState &otaDisplayState();

} // namespace gui::esp32
