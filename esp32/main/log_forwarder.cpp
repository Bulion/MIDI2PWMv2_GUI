#include "log_forwarder.h"

#include "libcomm/log_endpoint.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace gui::esp32
{
namespace
{

constexpr uart_port_t kUartPort = UART_NUM_0;
constexpr size_t kQueueCapacity = 16;
constexpr size_t kTagMaxLength = 32;
constexpr size_t kMessageMaxLength = 192;
constexpr int kSenderTaskStackSize = 8192;
constexpr int kSenderTaskPriority = 2;
constexpr int kSenderTaskCore = 1;

struct LogMessage {
    midi2pwm::log::LogLevel level;
    char tag[kTagMaxLength];
    char message[kMessageMaxLength];
};

QueueHandle_t logQueue = nullptr;
TaskHandle_t senderTaskHandle = nullptr;
vprintf_like_t originalVprintf = nullptr;
libcomm::LogEndpoint *logEndpoint = nullptr;
std::atomic<bool> insideSend{false};

bool logUartWrite(const std::uint8_t *data, std::size_t size)
{
    int written = uart_write_bytes(kUartPort, data, size);
    return written == static_cast<int>(size);
}

midi2pwm::log::LogLevel charToLogLevel(char levelChar)
{
    switch (levelChar) {
    case 'E': return midi2pwm::log::LogLevel::Error;
    case 'W': return midi2pwm::log::LogLevel::Warning;
    case 'D': return midi2pwm::log::LogLevel::Debug;
    default: return midi2pwm::log::LogLevel::Info;
    }
}

void parseEspLogFormat(const char *formatted, LogMessage &logMessage)
{
    logMessage.level = midi2pwm::log::LogLevel::Info;
    logMessage.tag[0] = '\0';

    if (formatted[0] == '\033') {
        const char *afterEscape = strchr(formatted, 'm');
        if (afterEscape) {
            formatted = afterEscape + 1;
        }
    }

    if (formatted[0] != '\0' && formatted[1] == ' ' && formatted[2] == '(') {
        logMessage.level = charToLogLevel(formatted[0]);

        const char *tagStart = nullptr;
        const char *closeParen = strchr(formatted + 2, ')');
        if (closeParen) {
            tagStart = closeParen + 1;
            while (*tagStart == ' ') {
                ++tagStart;
            }
        }

        if (tagStart) {
            const char *tagEnd = strchr(tagStart, ':');
            if (tagEnd) {
                size_t tagLength = static_cast<size_t>(tagEnd - tagStart);
                if (tagLength >= kTagMaxLength) {
                    tagLength = kTagMaxLength - 1;
                }
                memcpy(logMessage.tag, tagStart, tagLength);
                logMessage.tag[tagLength] = '\0';

                const char *messageStart = tagEnd + 1;
                while (*messageStart == ' ') {
                    ++messageStart;
                }
                strncpy(logMessage.message, messageStart, kMessageMaxLength - 1);
                logMessage.message[kMessageMaxLength - 1] = '\0';

                size_t messageLength = strlen(logMessage.message);
                while (messageLength > 0 &&
                       (logMessage.message[messageLength - 1] == '\n' ||
                        logMessage.message[messageLength - 1] == '\r' ||
                        logMessage.message[messageLength - 1] == '\033')) {
                    logMessage.message[--messageLength] = '\0';
                }

                if (messageLength > 0 && logMessage.message[messageLength - 1] == '[') {
                    logMessage.message[--messageLength] = '\0';
                }

                return;
            }
        }
    }

    strncpy(logMessage.tag, "ESP", kTagMaxLength - 1);
    logMessage.tag[kTagMaxLength - 1] = '\0';
    strncpy(logMessage.message, formatted, kMessageMaxLength - 1);
    logMessage.message[kMessageMaxLength - 1] = '\0';

    size_t messageLength = strlen(logMessage.message);
    while (messageLength > 0 &&
           (logMessage.message[messageLength - 1] == '\n' ||
            logMessage.message[messageLength - 1] == '\r')) {
        logMessage.message[--messageLength] = '\0';
    }
}

int customLogVprintf(const char *fmt, va_list args)
{
    if (insideSend.load(std::memory_order_relaxed)) {
        return 0;
    }

    char formatted[kTagMaxLength + kMessageMaxLength];
    int length = vsnprintf(formatted, sizeof(formatted), fmt, args);
    if (length <= 0) {
        return length;
    }

    LogMessage logMessage{};
    parseEspLogFormat(formatted, logMessage);

    if (logMessage.message[0] == '\0') {
        return length;
    }

    xQueueSend(logQueue, &logMessage, 0);

    return length;
}

void logSenderTask(void *param)
{
    (void)param;

    LogMessage logMessage{};

    while (true) {
        if (xQueueReceive(logQueue, &logMessage, portMAX_DELAY) == pdTRUE) {
            auto buffer = libcomm::BuildLogForwardMessage(
                logMessage.level, logMessage.tag, logMessage.message);

            insideSend.store(true, std::memory_order_relaxed);
            logEndpoint->Send(std::move(buffer));
            insideSend.store(false, std::memory_order_relaxed);
        }
    }
}

} // namespace

void initLogForwarder()
{
    static libcomm::LogEndpoint endpoint(
        libcomm::LogEndpoint::WriteCallback::create<&logUartWrite>());
    logEndpoint = &endpoint;

    logQueue = xQueueCreate(kQueueCapacity, sizeof(LogMessage));

    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        &logSenderTask,
        "log_fwd",
        kSenderTaskStackSize,
        nullptr,
        kSenderTaskPriority,
        &senderTaskHandle,
        kSenderTaskCore);

    if (taskCreated != pdPASS) {
        vQueueDelete(logQueue);
        logQueue = nullptr;
        return;
    }

    originalVprintf = esp_log_set_vprintf(&customLogVprintf);
}

void deinitLogForwarder()
{
    if (originalVprintf) {
        esp_log_set_vprintf(originalVprintf);
        originalVprintf = nullptr;
    }

    if (senderTaskHandle) {
        vTaskDelete(senderTaskHandle);
        senderTaskHandle = nullptr;
    }

    if (logQueue) {
        vQueueDelete(logQueue);
        logQueue = nullptr;
    }

    logEndpoint = nullptr;
}

} // namespace gui::esp32
