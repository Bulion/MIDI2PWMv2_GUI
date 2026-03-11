#pragma once

#include "libcomm/flash_writer.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include <cstdint>

class Esp32FlashWriter : public libcomm::IFlashWriter {
public:
    bool begin(std::uint32_t firmwareSize) override;
    bool writeChunk(std::uint16_t index, const std::uint8_t* data, std::size_t len) override;
    bool finish() override;
    bool verify(std::uint32_t expectedCrc32) override;
    bool activate() override;
    void abort() override;

private:
    esp_ota_handle_t otaHandle_{0};
    const esp_partition_t* updatePartition_{nullptr};
    std::uint32_t firmwareSize_{0};
    std::uint32_t bytesWritten_{0};
    bool inProgress_{false};
};
