#pragma once

#include "libcomm/flash_writer.h"

#include <cstdint>

class PsramBufferedFlashWriter : public libcomm::IFlashWriter {
public:
    explicit PsramBufferedFlashWriter(libcomm::IFlashWriter& delegate);
    ~PsramBufferedFlashWriter();

    PsramBufferedFlashWriter(const PsramBufferedFlashWriter&) = delete;
    PsramBufferedFlashWriter& operator=(const PsramBufferedFlashWriter&) = delete;

    void setCompressedTransfer(std::uint32_t compressedSize, std::uint32_t compressedCrc32,
                               std::uint32_t uncompressedSize);

    bool begin(std::uint32_t firmwareSize) override;
    bool writeChunk(std::uint16_t index, const std::uint8_t* data, std::size_t len) override;
    bool finish() override;
    bool verify(std::uint32_t expectedCrc32) override;
    bool activate() override;
    void abort() override;

private:
    bool isCompressed() const { return compressedSize_ > 0; }
    void freePsramBuffer();

    libcomm::IFlashWriter& delegate_;
    std::uint8_t* psramBuffer_{nullptr};
    std::uint32_t compressedSize_{0};
    std::uint32_t compressedCrc32_{0};
    std::uint32_t uncompressedSize_{0};
    std::uint32_t bytesBuffered_{0};
};
