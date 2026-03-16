#include "psram_buffered_flash_writer.h"

#include "gui_common/log.h"

#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <zlib.h>

static constexpr const char* TAG = "PsramFlashWriter";
static constexpr std::size_t INFLATE_CHUNK_SIZE = 4096;
static constexpr std::uint16_t WDT_YIELD_INTERVAL = 10;

static voidpf psramZalloc(voidpf, uInt items, uInt size)
{
    return heap_caps_malloc(static_cast<std::size_t>(items) * size, MALLOC_CAP_SPIRAM);
}

static void psramZfree(voidpf, voidpf address)
{
    heap_caps_free(address);
}

PsramBufferedFlashWriter::PsramBufferedFlashWriter(libcomm::IFlashWriter& delegate)
    : delegate_(delegate)
{
}

PsramBufferedFlashWriter::~PsramBufferedFlashWriter()
{
    freePsramBuffer();
}

void PsramBufferedFlashWriter::setCompressedTransfer(std::uint32_t compressedSize,
                                                     std::uint32_t compressedCrc32,
                                                     std::uint32_t uncompressedSize)
{
    compressedSize_ = compressedSize;
    compressedCrc32_ = compressedCrc32;
    uncompressedSize_ = uncompressedSize;

    if (isCompressed()) {
        GUI_LOG_INFO(TAG, "Compressed transfer: %lu -> %lu bytes (%.0f%%)",
                     static_cast<unsigned long>(compressedSize),
                     static_cast<unsigned long>(uncompressedSize),
                     100.0f * static_cast<float>(compressedSize) / static_cast<float>(uncompressedSize));
    } else {
        GUI_LOG_INFO(TAG, "Uncompressed transfer: %lu bytes",
                     static_cast<unsigned long>(uncompressedSize));
    }
}

bool PsramBufferedFlashWriter::begin(std::uint32_t firmwareSize)
{
    freePsramBuffer();
    bytesBuffered_ = 0;

    if (!isCompressed()) {
        GUI_LOG_INFO(TAG, "Uncompressed mode: writing directly to flash");
        return delegate_.begin(firmwareSize);
    }

    psramBuffer_ = static_cast<std::uint8_t*>(
        heap_caps_malloc(compressedSize_, MALLOC_CAP_SPIRAM));
    if (!psramBuffer_) {
        GUI_LOG_ERROR(TAG, "Failed to allocate %lu bytes from PSRAM",
                      static_cast<unsigned long>(compressedSize_));
        return false;
    }

    GUI_LOG_INFO(TAG, "Allocated %lu bytes in PSRAM for compressed firmware",
                 static_cast<unsigned long>(compressedSize_));
    return true;
}

bool PsramBufferedFlashWriter::writeChunk(std::uint16_t index, const std::uint8_t* data, std::size_t len)
{
    if (!isCompressed()) {
        return delegate_.writeChunk(index, data, len);
    }

    if (!psramBuffer_) {
        return false;
    }

    if (bytesBuffered_ + len > compressedSize_) {
        GUI_LOG_ERROR(TAG, "PSRAM buffer overflow at chunk %u: buffered=%lu + len=%lu > size=%lu",
                      index,
                      static_cast<unsigned long>(bytesBuffered_),
                      static_cast<unsigned long>(len),
                      static_cast<unsigned long>(compressedSize_));
        return false;
    }

    std::memcpy(psramBuffer_ + bytesBuffered_, data, len);
    bytesBuffered_ += static_cast<std::uint32_t>(len);
    return true;
}

bool PsramBufferedFlashWriter::finish()
{
    if (!isCompressed()) {
        return delegate_.finish();
    }

    if (!psramBuffer_) {
        return false;
    }

    std::uint32_t crc = esp_crc32_le(0, psramBuffer_, bytesBuffered_);
    GUI_LOG_INFO(TAG, "Compressed CRC: computed=0x%08lX expected=0x%08lX bytes=%lu",
                 static_cast<unsigned long>(crc),
                 static_cast<unsigned long>(compressedCrc32_),
                 static_cast<unsigned long>(bytesBuffered_));

    if (crc != compressedCrc32_) {
        GUI_LOG_ERROR(TAG, "Compressed CRC mismatch");
        freePsramBuffer();
        return false;
    }

    GUI_LOG_INFO(TAG, "Compressed CRC verified, erasing flash for %lu bytes",
                 static_cast<unsigned long>(uncompressedSize_));

    if (!delegate_.begin(uncompressedSize_)) {
        GUI_LOG_ERROR(TAG, "Delegate begin failed");
        freePsramBuffer();
        return false;
    }

    vTaskDelay(1);
    GUI_LOG_INFO(TAG, "Flash erased, starting inflate");

    z_stream stream{};
    stream.zalloc = psramZalloc;
    stream.zfree = psramZfree;

    int ret = inflateInit(&stream);
    if (ret != Z_OK) {
        GUI_LOG_ERROR(TAG, "inflateInit failed: %d", ret);
        delegate_.abort();
        freePsramBuffer();
        return false;
    }

    stream.next_in = psramBuffer_;
    stream.avail_in = bytesBuffered_;

    std::uint8_t outBuf[INFLATE_CHUNK_SIZE];
    std::uint16_t chunkIndex = 0;
    bool inflateOk = true;

    while (stream.avail_in > 0 || ret != Z_STREAM_END) {
        stream.next_out = outBuf;
        stream.avail_out = INFLATE_CHUNK_SIZE;

        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            GUI_LOG_ERROR(TAG, "inflate failed: %d at total_out=%lu", ret,
                          static_cast<unsigned long>(stream.total_out));
            inflateOk = false;
            break;
        }

        std::size_t produced = INFLATE_CHUNK_SIZE - stream.avail_out;
        if (produced > 0) {
            if (!delegate_.writeChunk(chunkIndex, outBuf, produced)) {
                GUI_LOG_ERROR(TAG, "Delegate writeChunk failed at inflate chunk %u", chunkIndex);
                inflateOk = false;
                break;
            }
            ++chunkIndex;
        }

        if (chunkIndex % WDT_YIELD_INTERVAL == 0) {
            vTaskDelay(1);
        }

        if (ret == Z_STREAM_END) {
            break;
        }
    }

    std::uint32_t totalOut = static_cast<std::uint32_t>(stream.total_out);
    inflateEnd(&stream);
    freePsramBuffer();

    if (!inflateOk) {
        delegate_.abort();
        return false;
    }

    GUI_LOG_INFO(TAG, "Inflate complete: %lu bytes decompressed in %u chunks",
                 static_cast<unsigned long>(totalOut), chunkIndex);

    if (totalOut != uncompressedSize_) {
        GUI_LOG_ERROR(TAG, "Decompressed size mismatch: got %lu expected %lu",
                      static_cast<unsigned long>(totalOut),
                      static_cast<unsigned long>(uncompressedSize_));
        delegate_.abort();
        return false;
    }

    return delegate_.finish();
}

bool PsramBufferedFlashWriter::verify(std::uint32_t expectedCrc32)
{
    return delegate_.verify(expectedCrc32);
}

bool PsramBufferedFlashWriter::activate()
{
    return delegate_.activate();
}

void PsramBufferedFlashWriter::abort()
{
    freePsramBuffer();
    delegate_.abort();
}

std::uint32_t PsramBufferedFlashWriter::maxFirmwareSize() const
{
    return delegate_.maxFirmwareSize();
}

void PsramBufferedFlashWriter::freePsramBuffer()
{
    if (psramBuffer_) {
        heap_caps_free(psramBuffer_);
        psramBuffer_ = nullptr;
    }
    bytesBuffered_ = 0;
}
