#include "esp32_flash_writer.h"

#include "gui_common/log.h"

#include "esp_crc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

bool Esp32FlashWriter::begin(std::uint32_t firmwareSize)
{
    updatePartition_ = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition_) {
        GUI_LOG_ERROR("Esp32FlashWriter", "No OTA update partition found");
        return false;
    }

    GUI_LOG_INFO("Esp32FlashWriter", "OTA begin: size=%lu, partition=%s",
                 static_cast<unsigned long>(firmwareSize),
                 updatePartition_->label);

    esp_err_t err = esp_ota_begin(updatePartition_, firmwareSize, &otaHandle_);
    if (err != ESP_OK) {
        GUI_LOG_ERROR("Esp32FlashWriter", "esp_ota_begin failed: %s", esp_err_to_name(err));
        return false;
    }

    firmwareSize_ = firmwareSize;
    bytesWritten_ = 0;
    inProgress_ = true;
    return true;
}

bool Esp32FlashWriter::writeChunk(std::uint16_t index, const std::uint8_t* data, std::size_t len)
{
    if (!inProgress_) {
        return false;
    }

    esp_err_t err = esp_ota_write(otaHandle_, data, len);
    if (err != ESP_OK) {
        GUI_LOG_ERROR("Esp32FlashWriter", "esp_ota_write failed at chunk %u: %s",
                      index, esp_err_to_name(err));
        return false;
    }

    bytesWritten_ += static_cast<std::uint32_t>(len);
    return true;
}

bool Esp32FlashWriter::finish()
{
    if (!inProgress_) {
        return false;
    }

    esp_err_t err = esp_ota_end(otaHandle_);
    if (err != ESP_OK) {
        GUI_LOG_ERROR("Esp32FlashWriter", "esp_ota_end failed: %s", esp_err_to_name(err));
        otaHandle_ = 0;
        return false;
    }

    GUI_LOG_INFO("Esp32FlashWriter", "OTA finish: wrote %lu bytes",
                 static_cast<unsigned long>(bytesWritten_));

    otaHandle_ = 0;
    return true;
}

bool Esp32FlashWriter::verify(std::uint32_t expectedCrc32)
{
    if (!inProgress_ || !updatePartition_) {
        return false;
    }

    static constexpr std::size_t READ_BUF_SIZE = 4096;
    std::uint8_t buf[READ_BUF_SIZE];
    std::uint32_t crc = 0;
    std::uint32_t remaining = bytesWritten_;
    std::uint32_t offset = 0;

    while (remaining > 0) {
        std::size_t toRead = (remaining > READ_BUF_SIZE) ? READ_BUF_SIZE : remaining;
        esp_err_t err = esp_partition_read(updatePartition_, offset, buf, toRead);
        if (err != ESP_OK) {
            GUI_LOG_ERROR("Esp32FlashWriter", "Partition read failed at offset %lu: %s",
                          static_cast<unsigned long>(offset), esp_err_to_name(err));
            return false;
        }
        crc = esp_crc32_le(crc, buf, toRead);
        offset += static_cast<std::uint32_t>(toRead);
        remaining -= static_cast<std::uint32_t>(toRead);
    }

    if (crc != expectedCrc32) {
        GUI_LOG_ERROR("Esp32FlashWriter", "CRC mismatch: expected 0x%08lX, got 0x%08lX",
                      static_cast<unsigned long>(expectedCrc32),
                      static_cast<unsigned long>(crc));
        return false;
    }

    GUI_LOG_INFO("Esp32FlashWriter", "CRC verified: 0x%08lX", static_cast<unsigned long>(crc));
    return true;
}

bool Esp32FlashWriter::activate()
{
    if (!inProgress_ || !updatePartition_) {
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(updatePartition_);
    if (err != ESP_OK) {
        GUI_LOG_ERROR("Esp32FlashWriter", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return false;
    }

    GUI_LOG_INFO("Esp32FlashWriter", "Activated partition %s, rebooting...",
                 updatePartition_->label);

    inProgress_ = false;
    esp_restart();
    return true;
}

void Esp32FlashWriter::abort()
{
    if (inProgress_ && otaHandle_ != 0) {
        esp_ota_abort(otaHandle_);
        otaHandle_ = 0;
    }
    inProgress_ = false;
    bytesWritten_ = 0;
    firmwareSize_ = 0;
    updatePartition_ = nullptr;

    GUI_LOG_INFO("Esp32FlashWriter", "OTA aborted");
}
