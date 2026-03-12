#pragma once

#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
    #define PLATFORM_DESKTOP
#elif defined(ESP_PLATFORM)
    #define PLATFORM_ESP32
#else
    #error "Unsupported platform"
#endif

#define GUI_LOG_LEVEL_NONE    0
#define GUI_LOG_LEVEL_ERROR   1
#define GUI_LOG_LEVEL_WARNING 2
#define GUI_LOG_LEVEL_INFO    3
#define GUI_LOG_LEVEL_DEBUG   4
#define GUI_LOG_LEVEL_VERBOSE 5

#ifndef GUI_LOG_LEVEL
    #ifdef NDEBUG
        #define GUI_LOG_LEVEL GUI_LOG_LEVEL_INFO
    #else
        #define GUI_LOG_LEVEL GUI_LOG_LEVEL_DEBUG
    #endif
#endif

#ifdef PLATFORM_DESKTOP
    #include <cstdio>

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_ERROR
        #define GUI_LOG_ERROR(tag, format, ...) printf("[ERROR][%s] " format "\n", tag, ##__VA_ARGS__)
    #else
        #define GUI_LOG_ERROR(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_WARNING
        #define GUI_LOG_WARNING(tag, format, ...) printf("[WARNING][%s] " format "\n", tag, ##__VA_ARGS__)
    #else
        #define GUI_LOG_WARNING(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_INFO
        #define GUI_LOG_INFO(tag, format, ...) printf("[INFO][%s] " format "\n", tag, ##__VA_ARGS__)
    #else
        #define GUI_LOG_INFO(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_DEBUG
        #define GUI_LOG_DEBUG(tag, format, ...) printf("[DEBUG][%s] " format "\n", tag, ##__VA_ARGS__)
    #else
        #define GUI_LOG_DEBUG(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_VERBOSE
        #define GUI_LOG_VERBOSE(tag, format, ...) printf("[VERBOSE][%s] " format "\n", tag, ##__VA_ARGS__)
    #else
        #define GUI_LOG_VERBOSE(tag, format, ...) ((void)0)
    #endif
#endif

#ifdef PLATFORM_ESP32
    #include "esp_log.h"

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_ERROR
        #define GUI_LOG_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
    #else
        #define GUI_LOG_ERROR(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_WARNING
        #define GUI_LOG_WARNING(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
    #else
        #define GUI_LOG_WARNING(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_INFO
        #define GUI_LOG_INFO(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #else
        #define GUI_LOG_INFO(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_DEBUG
        #define GUI_LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
    #else
        #define GUI_LOG_DEBUG(tag, format, ...) ((void)0)
    #endif

    #if GUI_LOG_LEVEL >= GUI_LOG_LEVEL_VERBOSE
        #define GUI_LOG_VERBOSE(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)
    #else
        #define GUI_LOG_VERBOSE(tag, format, ...) ((void)0)
    #endif
#endif

#include <cstdarg>
#include <cstdio>
#include "libcomm/logging.h"

namespace gui::common
{

inline void BridgeLibcommToGuiLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args)
{
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);

    switch (level) {
        case libcomm::LogLevel::Error:
            GUI_LOG_ERROR(tag, "%s", buffer);
            break;
        case libcomm::LogLevel::Warning:
            GUI_LOG_WARNING(tag, "%s", buffer);
            break;
        case libcomm::LogLevel::Info:
            GUI_LOG_INFO(tag, "%s", buffer);
            break;
        case libcomm::LogLevel::Debug:
            GUI_LOG_DEBUG(tag, "%s", buffer);
            break;
    }
}

inline void InitializeLibcommLogging()
{
    libcomm::SetGlobalLogger(libcomm::LogSink::create<&BridgeLibcommToGuiLogger>());
}

} // namespace gui::common
