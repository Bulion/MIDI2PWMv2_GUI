# GUI Logging System

A unified logging system that works across both desktop (Linux/Windows/macOS) and ESP32 platforms.

## Available Log Levels

The logging system provides 5 log levels aligned with ESP-IDF logging:

- `GUI_LOG_ERROR(tag, format, ...)` - Error messages (critical failures)
- `GUI_LOG_WARNING(tag, format, ...)` - Warning messages (potential issues)
- `GUI_LOG_INFO(tag, format, ...)` - Informational messages (important events)
- `GUI_LOG_DEBUG(tag, format, ...)` - Debug messages (detailed flow information)
- `GUI_LOG_VERBOSE(tag, format, ...)` - Verbose messages (very detailed information)

## Usage

```cpp
#include "gui_common/log.h"

void myFunction()
{
    GUI_LOG_INFO("MyComponent", "Started initialization");
    GUI_LOG_DEBUG("MyComponent", "Processing %d items", itemCount);

    if (error) {
        GUI_LOG_ERROR("MyComponent", "Failed to process: %s", errorMessage);
    }

    GUI_LOG_VERBOSE("MyComponent", "Detail: value=%d, state=%d", value, state);
}
```

## Configuring Log Level

### Method 1: Automatic (Default)
By default, the log level is automatically set based on build type:
- **Debug builds** (`NDEBUG` not defined): `GUI_LOG_LEVEL_DEBUG`
- **Release builds** (`NDEBUG` defined): `GUI_LOG_LEVEL_INFO`

### Method 2: CMake Compile Definition
Set the log level globally in your CMakeLists.txt:

```cmake
# Set to ERROR level (only errors shown)
target_compile_definitions(my_target PRIVATE GUI_LOG_LEVEL=1)

# Set to WARNING level (errors + warnings)
target_compile_definitions(my_target PRIVATE GUI_LOG_LEVEL=2)

# Set to INFO level (errors + warnings + info)
target_compile_definitions(my_target PRIVATE GUI_LOG_LEVEL=3)

# Set to DEBUG level (errors + warnings + info + debug)
target_compile_definitions(my_target PRIVATE GUI_LOG_LEVEL=4)

# Set to VERBOSE level (all messages)
target_compile_definitions(my_target PRIVATE GUI_LOG_LEVEL=5)

# Disable all logging
target_compile_definitions(my_target PRIVATE GUI_LOG_LEVEL=0)
```

### Method 3: Compiler Flag
Pass the log level directly to the compiler:

```bash
# Debug builds with verbose logging
cmake -DCMAKE_CXX_FLAGS="-DGUI_LOG_LEVEL=5" ..

# Release builds with only errors
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-DGUI_LOG_LEVEL=1" ..
```

### Method 4: Per-File Override
Define the log level before including the header in specific files:

```cpp
#define GUI_LOG_LEVEL 5  // Enable verbose logging for this file only
#include "gui_common/log.h"
```

## Log Level Constants

Use these constants when setting log levels:

- `GUI_LOG_LEVEL_NONE` (0) - No logging
- `GUI_LOG_LEVEL_ERROR` (1) - Only errors
- `GUI_LOG_LEVEL_WARNING` (2) - Errors + warnings
- `GUI_LOG_LEVEL_INFO` (3) - Errors + warnings + info
- `GUI_LOG_LEVEL_DEBUG` (4) - Errors + warnings + info + debug
- `GUI_LOG_LEVEL_VERBOSE` (5) - All messages

## Platform-Specific Behavior

### Desktop (Linux/Windows/macOS)
Logs are printed to stdout using printf with format:
```
[LEVEL][Tag] message
```

Example output:
```
[INFO][SerialBackend] Connected to /dev/ttyACM0 at 921600 baud
[DEBUG][SerialBackend] Reader thread started
[VERBOSE][SerialBackend] Read 64 bytes
[ERROR][SerialBackend] Failed to open port: /dev/ttyACM1 (error: 2)
```

### ESP32
Uses ESP-IDF's logging system (`esp_log.h`):
- Integrates with ESP32 log level configuration
- Supports colored output on UART console
- Can be filtered per-component using `esp_log_level_set()`
- Includes timestamp and task information

## Performance Considerations

- Log statements at disabled levels compile to `((void)0)` - zero runtime overhead
- String formatting only occurs if the log level is enabled
- On ESP32, uses ESP-IDF's efficient logging infrastructure
- VERBOSE logs can impact performance - use sparingly in time-critical code

## Best Practices

1. **Use appropriate log levels:**
   - ERROR: Failures that prevent functionality
   - WARNING: Issues that don't prevent operation but may cause problems
   - INFO: Important state changes and events
   - DEBUG: Detailed flow information for debugging
   - VERBOSE: Very detailed information (e.g., every byte read)

2. **Tag naming:**
   - Use descriptive component names: `"SerialBackend"`, `"FrameParser"`
   - Keep tags consistent within a module
   - Max 23 characters on ESP32 (ESP-IDF limitation)

3. **Production builds:**
   - Set log level to INFO or WARNING in release builds
   - Never rely on log output for program correctness

4. **Avoid logging in hot paths:**
   - Don't use VERBOSE logs in tight loops
   - Consider conditional compilation for performance-critical sections

## Example: Different Log Levels

```cpp
void processData(const uint8_t* data, size_t size)
{
    GUI_LOG_DEBUG("DataProcessor", "Processing %zu bytes", size);

    for (size_t i = 0; i < size; i++) {
        GUI_LOG_VERBOSE("DataProcessor", "Byte[%zu] = 0x%02X", i, data[i]);

        if (data[i] == SYNC_BYTE) {
            GUI_LOG_DEBUG("DataProcessor", "Found sync byte at position %zu", i);
        }
    }

    if (size == 0) {
        GUI_LOG_WARNING("DataProcessor", "Received empty data buffer");
    }

    bool success = internalProcess(data, size);
    if (!success) {
        GUI_LOG_ERROR("DataProcessor", "Failed to process data");
    } else {
        GUI_LOG_INFO("DataProcessor", "Successfully processed %zu bytes", size);
    }
}
```

With `GUI_LOG_LEVEL=GUI_LOG_LEVEL_INFO`, you'll see:
- ✅ ERROR message (if processing fails)
- ✅ WARNING message (if empty buffer)
- ✅ INFO message (on success)
- ❌ DEBUG messages (filtered out at compile time)
- ❌ VERBOSE messages (filtered out at compile time)
