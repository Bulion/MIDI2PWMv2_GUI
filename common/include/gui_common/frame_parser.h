#pragma once

#include <cstddef>
#include <cstdint>

#include "etl/array.h"
#include "etl/delegate.h"
#include "libcomm/frame_transport.h"

namespace gui::common
{

class FrameParser
{
public:
    using FrameCallback = etl::delegate<void(const std::uint8_t *, std::size_t)>;

    void reset();
    void feed(const std::uint8_t *data, std::size_t size);
    void setCallback(FrameCallback callback);

private:
    static constexpr std::size_t kBufferCapacity =
        libcomm::FrameTransport::kHeaderSize + libcomm::FrameTransport::kMaxFrameSize +
        libcomm::FrameTransport::kCrcSize;

    FrameCallback callback_;
    etl::array<std::uint8_t, kBufferCapacity> buffer_{};
    std::size_t size_{0};
};

} // namespace gui::common
