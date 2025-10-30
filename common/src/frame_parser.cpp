#include "gui_common/frame_parser.h"

#include "flatbuffers/base.h"
#include "gui_common/log.h"

#include <cstdint>
#include <cstring>

#include "etl/algorithm.h"

namespace gui::common
{

static constexpr const char* TAG = "FrameParser";

void FrameParser::reset()
{
    size_ = 0U;
}

void FrameParser::setCallback(FrameCallback callback)
{
    callback_ = callback;
}

void FrameParser::feed(const std::uint8_t *data, std::size_t size)
{
    if (!data || size == 0U) {
        return;
    }

    GUI_LOG_DEBUG(TAG, "feed() called: received %zu bytes from serial", size);

    if (size >= 12) {
        GUI_LOG_DEBUG(TAG, "First 12 bytes (hex): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                      data[0], data[1], data[2], data[3],
                      data[4], data[5], data[6], data[7],
                      data[8], data[9], data[10], data[11]);
    }

    if (!callback_.is_valid()) {
        GUI_LOG_ERROR(TAG, "Frame callback not registered!");
        return;
    }

    std::size_t spaceAvailable = buffer_.size() - size_;
    if (size > spaceAvailable) {
        GUI_LOG_ERROR(TAG, "Buffer overflow: %zu bytes available, %zu bytes received - resetting buffer",
                      spaceAvailable, size);
        size_ = 0;
    }

    etl::copy(data, data + size, buffer_.begin() + size_);
    size_ += size;

    GUI_LOG_DEBUG(TAG, "Buffer now contains %zu bytes", size_);

    constexpr std::size_t MINIMUM_FRAME_SIZE = libcomm::FrameTransport::kHeaderSize + libcomm::FrameTransport::kCrcSize;

    auto dummyWriteCallback = [](const std::uint8_t*, std::size_t) -> bool {
        return false;
    };

    auto dataHandler = [this](const std::uint8_t *payload, std::size_t payloadSize) -> bool {
        GUI_LOG_INFO(TAG, "Extracted FlatBuffer payload: %zu bytes", payloadSize);

        if (payloadSize >= 8) {
            GUI_LOG_DEBUG(TAG, "Payload first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                          payload[0], payload[1], payload[2], payload[3],
                          payload[4], payload[5], payload[6], payload[7]);
        }

        callback_(payload, payloadSize);
        return true;
    };

    libcomm::FrameTransport::WriteCallback writeCallback =
        libcomm::FrameTransport::WriteCallback::create(dummyWriteCallback);

    libcomm::FrameTransport frameTransport(writeCallback, true);

    while (size_ >= MINIMUM_FRAME_SIZE) {
        if (size_ < libcomm::FrameTransport::kHeaderSize) {
            break;
        }

        std::uint32_t payloadSizeBytesLittleEndian = 0;
        std::memcpy(&payloadSizeBytesLittleEndian, &buffer_[4], sizeof(payloadSizeBytesLittleEndian));
        std::uint32_t payloadSizeBytes = flatbuffers::EndianScalar(payloadSizeBytesLittleEndian);

        std::size_t expectedTotalSizeBytes = libcomm::FrameTransport::kHeaderSize +
                                              static_cast<std::size_t>(payloadSizeBytes) +
                                              libcomm::FrameTransport::kCrcSize;

        if (expectedTotalSizeBytes > buffer_.size()) {
            GUI_LOG_ERROR(TAG, "Invalid frame size in header: %zu exceeds buffer capacity %zu - resetting",
                          expectedTotalSizeBytes, buffer_.size());
            size_ = 0;
            break;
        }

        if (size_ < expectedTotalSizeBytes) {
            GUI_LOG_DEBUG(TAG, "Incomplete frame: have %zu bytes, need %zu bytes - waiting for more data",
                          size_, expectedTotalSizeBytes);
            break;
        }

        bool frameProcessedSuccessfully = frameTransport.HandleIncoming(buffer_.data(), expectedTotalSizeBytes, dataHandler);

        if (frameProcessedSuccessfully) {
            GUI_LOG_DEBUG(TAG, "Frame processed successfully, consumed %zu bytes", expectedTotalSizeBytes);

            std::size_t remainingBytes = size_ - expectedTotalSizeBytes;
            if (remainingBytes > 0) {
                etl::copy(buffer_.begin() + expectedTotalSizeBytes, buffer_.begin() + size_, buffer_.begin());
                GUI_LOG_DEBUG(TAG, "Moved %zu remaining bytes to start of buffer", remainingBytes);
            }
            size_ = remainingBytes;
        } else {
            GUI_LOG_WARNING(TAG, "Failed to process frame (invalid header, CRC, or size) - discarding %zu bytes",
                            expectedTotalSizeBytes);
            std::size_t remainingBytes = size_ - expectedTotalSizeBytes;
            if (remainingBytes > 0) {
                etl::copy(buffer_.begin() + expectedTotalSizeBytes, buffer_.begin() + size_, buffer_.begin());
            }
            size_ = remainingBytes;
        }
    }
}

} // namespace gui::common
