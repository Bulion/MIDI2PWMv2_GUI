# MIDI2PWMv2 GUI - Project-Specific Guidelines

## Project Overview

This is a multi-platform GUI application for monitoring and configuring the MIDI2PWMv2 device. The UI is built with Slint (formerly SixtyFPS), a modern declarative UI framework designed for embedded and desktop applications. The project supports both native desktop builds and ESP32-based embedded displays.

## Technology Stack

- **UI Framework**: Slint (declarative UI language)
- **Language**: C++20 (application logic), Slint (UI markup)
- **Build System**: CMake 3.24+
- **Platforms**:
  - Desktop: Native builds (Linux, Windows, macOS)
  - Embedded: ESP32 with IDF (Espressif IoT Development Framework)
- **Communication**: libcomm (shared protocol with firmware)

## Architecture

### Project Structure

```
GUI/
├── ui/                              # Slint UI files (.slint)
│   ├── app-window.slint            # Main application window
│   ├── channel-config-popup.slint  # Channel configuration dialog
│   ├── gauge-ring.slint            # Circular gauge component
│   └── linear-gauge.slint          # Linear gauge component
├── common/                          # Shared code between desktop and ESP32
│   ├── CMakeLists.txt
│   ├── include/gui_common/
│   │   ├── backends/
│   │   │   └── connection_backend.h      # Abstract platform backend
│   │   ├── view_models/
│   │   │   ├── connection_view_model.h   # Connection state machine
│   │   │   ├── midi_message_view_model.h # MIDI message display
│   │   │   └── channel_telemetry_view_model.h # PWM channel data
│   │   ├── frame_parser.h                # Serial frame parsing
│   │   ├── message_processor.h           # FlatBuffers message routing
│   │   └── log.h                         # Compile-time logging macros
│   └── src/
│       ├── frame_parser.cpp
│       ├── message_processor.cpp
│       └── view_models/
├── desktop/                         # Desktop-specific implementation
│   ├── src/
│   │   ├── main.cpp                # Desktop entry point with MVVM wiring
│   │   ├── serial_backend.h        # CSerialPort implementation
│   │   └── serial_backend.cpp
│   └── CMakeLists.txt
├── esp32/                           # ESP32-specific implementation
│   ├── main/
│   │   ├── main.cpp                # ESP32 entry point with MVVM wiring
│   │   ├── uart_backend.h          # ESP-IDF UART implementation
│   │   └── uart_backend.cpp
│   └── CMakeLists.txt
└── CMakeLists.txt                   # Top-level build configuration
```

### Architecture Layers

#### 1. UI Layer (Slint)
- **app-window.slint**: Main window with connection screen, channel grid, and MIDI display
- **channel-config-popup.slint**: Modal dialog for configuring PWM channel parameters
- **Custom components**: Reusable gauge components for visual feedback

#### 2. View Model Layer (MVVM Pattern)
- **ConnectionViewModel**: State machine managing connection lifecycle
  - States: Disconnected → WaitingForHeartBeatResponse → WaitingForTelemetryData → FullyConnected
  - Handles message routing and platform backend abstraction
- **MidiMessageViewModel**: Formats MIDI channel messages for display
- **ChannelTelemetryViewModel**: Manages 16-channel telemetry array (voltage, current, config, faults)

#### 3. Communication Layer
- **FrameParser**: CRC-validated framed protocol parser
- **MessageProcessor**: FlatBuffers message serialization/deserialization using libcomm schema
- **Bidirectional protocol**: Telemetry, MIDI, heartbeat (device→GUI), configuration (GUI→device)

#### 4. Platform Backend Layer (Abstract Interface)
- **ConnectionBackend**: Pure virtual interface for platform abstraction
- **SerialBackend** (desktop): CSerialPort cross-platform serial communication
- **UartBackend** (ESP32): ESP-IDF UART driver with FreeRTOS task

#### 5. Application Layer
- Platform-specific main() wiring view models, backends, and Slint UI
- Thread-safe callbacks using slint::invoke_from_event_loop (desktop) or direct calls (ESP32)
- Interactive features: MIDI note assignment workflow, channel configuration

### Key Design Patterns

- **MVVM (Model-View-ViewModel)**: Clean separation between UI and business logic
- **Observer Pattern**: ETL delegates for view model → UI updates
- **State Machine**: Explicit connection states with transition logic
- **Dependency Injection**: ViewModels receive backend references via constructor
- **Platform Abstraction**: ConnectionBackend interface isolates platform-specific code

## Slint UI Framework Best Practices

### Slint Language Basics

Slint is a declarative UI language with similarities to QML but designed for performance and portability. Key principles:

- **Declarative syntax**: Describe what the UI should look like, not how to build it
- **Property bindings**: Reactive updates when data changes
- **Component composition**: Build complex UIs from reusable components
- **Callbacks**: Communication between UI and C++ backend

### Component Structure

Follow this order in Slint components:
1. Import statements
2. Component definition
3. Properties (exported first, then internal)
4. Callbacks
5. Layout and visual elements

Example:
```slint
import { VerticalBox } from "std-widgets.slint";

export component ChannelCard {
    // Exported properties (public API)
    in property <int> channel-number;
    in property <float> current-value;
    out property <bool> is-active;

    // Internal properties
    private property <color> base-color: #3498db;

    // Callbacks
    callback clicked();

    // Layout and visuals
    VerticalBox {
        Text { text: "Channel \{channel-number}"; }
    }
}
```

### Property Naming

- Use kebab-case for Slint properties: `channel-number`, `is-active`, `base-color`
- Use snake_case for C++ bindings: `channel_number`, `is_active`, `base_color`
- Prefix with type hints:
  - `is-*` for booleans: `is-active`, `is-enabled`
  - `*-color` for colors: `base-color`, `accent-color`
  - `*-text` for strings: `button-text`, `label-text`

### Data Binding

- **One-way binding (in)**: Data flows from C++ to Slint
  ```slint
  in property <int> value;
  ```

- **One-way binding (out)**: Data flows from Slint to C++
  ```slint
  out property <bool> is-pressed;
  ```

- **Two-way binding (in-out)**: Bidirectional data flow
  ```slint
  in-out property <string> text;
  ```

- **Animations**: Use Slint's built-in animation support
  ```slint
  animate width { duration: 200ms; easing: ease-in-out; }
  ```

### Component Organization

- **Keep components focused**: One component per file for reusability
- **Composition over inheritance**: Build complex components from simpler ones
- **Shared styles**: Use global style properties for consistency
- **Responsive layout**: Use flexible layouts (VerticalBox, HorizontalBox, GridLayout)

### C++ Integration

Use Slint's generated C++ API for backend integration:

```cpp
#include "app-window.h"

int main() {
    auto app = AppWindow::create();

    // Set property from C++
    app->set_channel_count(8);

    // Connect callback
    app->on_channel_clicked([](int channel) {
        // Handle click
    });

    app->run();
}
```

### Performance Considerations

- **Minimize property bindings**: Complex bindings can impact performance
- **Avoid deep nesting**: Keep component hierarchy shallow when possible
- **Use @children for containers**: Allows efficient dynamic content
- **Lazy loading**: Consider lazy-loading for complex views not immediately visible

## Platform-Specific Guidelines

### Desktop Build

Build and run the desktop application:
```bash
cmake -B build -S . -DGUI_BUILD_DESKTOP=ON -DGUI_BUILD_ESP32=OFF
cmake --build build
./build/desktop/MIDI2PWMv2_GUI_Desktop
```

Desktop-specific considerations:
- **Window management**: Native window decorations and resizing
- **High DPI**: Slint handles DPI scaling automatically
- **Input devices**: Full keyboard and mouse support
- **Performance**: Desktop CPUs allow more complex UI than embedded

### ESP32 Build

Build ESP32 firmware:
```bash
cd esp32
idf.py build
idf.py flash monitor
```

ESP32-specific considerations:
- **Display driver**: Uses ESP-IDF LCD drivers for the specific display
- **Touch input**: ESP LCD Touch drivers (e.g., GT911)
- **Memory constraints**: ESP32 has limited RAM - keep UI lightweight
- **Flash storage**: UI resources can be stored in flash
- **Rendering**: Slint's MCU backend optimized for embedded displays
- **Startup time**: Consider splash screen during initialization

### Common Platform Abstraction

Place shared code in `common/` that works on both platforms:
- Communication protocol handling
- State management
- Business logic
- Data models

Use conditional compilation or runtime abstraction for platform-specific features.

## Logging System

The project uses a compile-time configurable logging system defined in [common/include/gui_common/log.h](common/include/gui_common/log.h).

### Log Levels

Five log levels with printf-style formatting:
- `GUI_LOG_VERBOSE(tag, format, ...)` - Detailed trace information
- `GUI_LOG_DEBUG(tag, format, ...)` - Debug information
- `GUI_LOG_INFO(tag, format, ...)` - Informational messages
- `GUI_LOG_WARNING(tag, format, ...)` - Warning messages
- `GUI_LOG_ERROR(tag, format, ...)` - Error messages

### Configuration

Set minimum log level at compile time via CMake:
```cmake
target_compile_definitions(gui_common PRIVATE GUI_LOG_LEVEL=GUI_LOG_LEVEL_INFO)
```

Log levels (ordered):
- `GUI_LOG_LEVEL_VERBOSE` (0) - All messages
- `GUI_LOG_LEVEL_DEBUG` (1)
- `GUI_LOG_LEVEL_INFO` (2)
- `GUI_LOG_LEVEL_WARNING` (3)
- `GUI_LOG_LEVEL_ERROR` (4)
- `GUI_LOG_LEVEL_NONE` (5) - No logging

### Platform Integration

Logging automatically adapts to platform:
- **Desktop**: Uses `printf()` to stdout
- **ESP32**: Uses ESP-IDF `ESP_LOG*` macros with color support
- **Compile-time filtering**: Logs below minimum level compile to nothing (zero overhead)

### Usage Example

```cpp
#include "gui_common/log.h"

void ConnectionViewModel::connect(const std::string &port) {
    GUI_LOG_INFO("ConnectionVM", "Attempting connection to %s", port.c_str());

    if (port.empty()) {
        GUI_LOG_ERROR("ConnectionVM", "Cannot connect: port is empty");
        return;
    }

    GUI_LOG_VERBOSE("ConnectionVM", "Opening serial port with baud rate %d", 115200);
    // ...
}
```

### Tag Conventions

Use descriptive tags for module identification:
- `ConnectionVM` - ConnectionViewModel
- `FrameParser` - FrameParser
- `MessageProc` - MessageProcessor
- `SerialBackend` - Desktop serial backend
- `UartBackend` - ESP32 UART backend

See [LOG_USAGE.md](common/include/gui_common/LOG_USAGE.md) for detailed documentation.

## libcomm Integration

### Protocol Overview
- **Schema**: FlatBuffers definitions shared between firmware and GUI (libcomm repository)
- **Message types**:
  - **Device→GUI**: PWM telemetry, MIDI channel messages, heartbeat acknowledgments, command responses
  - **GUI→Device**: Heartbeat with telemetry request flag, channel configuration commands
- **Framing**: CRC16-validated frames parsed by FrameParser
- **Serialization**: MessageProcessor handles FlatBuffers pack/unpack

### Platform Threading Model
- **Desktop**: Uses std::thread for serial read loop, std::mutex for state protection
- **ESP32**: Uses FreeRTOS xTaskCreate for UART read task, FreeRTOS mutexes
- **libcomm configuration**: Built in STL mode (LIBCOMM_ETL_NO_STL=OFF) for desktop, ETL mode for ESP32

### Transport Layer
- **Desktop**: CSerialPort over USB CDC (e.g., /dev/ttyACM0, COM3)
- **ESP32**: ESP-IDF UART driver (typically UART0 for internal ESP32-to-MCU communication)

### Message Flow Example
1. **Connection establishment**:
   - GUI sends HeartBeat with request_telemetry=true
   - Device responds with Response (ACK/NACK)
   - Device sends 16 ChannelTelemetry messages
   - GUI transitions to FullyConnected state

2. **Channel configuration**:
   - User modifies channel settings in GUI
   - GUI sends ChannelConfig via MessageProcessor
   - Device applies config and responds with Response
   - Device sends updated ChannelTelemetry

3. **MIDI monitoring**:
   - Device receives MIDI from synthesizer
   - Device forwards ChannelMessage to GUI
   - GUI updates MidiMessageViewModel and triggers note assignment if active

## Build System

### Top-Level CMake

The top-level [CMakeLists.txt](CMakeLists.txt) coordinates all builds:
- **libcomm integration**: Added as subdirectory with STL mode enabled
- **common library**: Added as subdirectory, built as static library (gui_common)
- **Conditional builds**: Options `GUI_BUILD_DESKTOP` (default: ON) and `GUI_BUILD_ESP32` (default: ON)
- **Configuration**:
  ```cmake
  set(LIBCOMM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LIBCOMM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LIBCOMM_ETL_NO_STL OFF CACHE BOOL "" FORCE)  # STL mode for desktop
  ```

### Common Library CMake

[common/CMakeLists.txt](common/CMakeLists.txt) creates gui_common static library:
- **Sources**: Frame parser, message processor, all view models
- **Dependencies**: Links libcomm
- **Headers**: Public interface in `include/gui_common/`
- **Namespace**: All code in `gui::common`

### Desktop CMake

[desktop/CMakeLists.txt](desktop/CMakeLists.txt) - Standard CMake project:
- **Dependencies fetched**:
  - Slint v1.12.1 (UI framework)
  - CSerialPort v4.3.1 (cross-platform serial communication)
- **Executable**: gui_desktop
- **Links**: Slint::Slint, gui_common, libcserialport, Threads::Threads
- **C++ standard**: C++20
- **Build**: Works with GCC, Clang, MSVC on Linux/Windows/macOS

### ESP32 Build

[esp32/CMakeLists.txt](esp32/CMakeLists.txt) - ESP-IDF project:
- **Build via idf.py**: Must use ESP-IDF toolchain (xtensa-esp32s3-elf-gcc)
- **main component**: Links gui_common and ESP-IDF components (driver, esp_lcd_touch, etc.)
- **Managed components**:
  - slint__slint (Slint for MCUs)
  - Various ESP-IDF LCD/touch drivers
- **libcomm mode**: Uses ETL (no STL) when building for ESP32
- **Cannot be built directly from top-level CMake**: Use `cd esp32 && idf.py build`

### Build Commands

**Desktop only**:
```bash
cmake -B build -S . -DGUI_BUILD_DESKTOP=ON -DGUI_BUILD_ESP32=OFF
cmake --build build
./build/desktop/gui_desktop
```

**ESP32 only**:
```bash
cd esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

**Both platforms** (requires ESP-IDF in PATH):
```bash
cmake -B build -S .
cmake --build build          # Builds desktop
cd esp32 && idf.py build     # Builds ESP32 separately
```

## Testing Strategy

### Desktop Testing

- **Rapid iteration**: Build and test UI changes quickly on desktop
- **Mock communication**: Simulate device communication for UI testing
- **Automated testing**: Slint supports UI testing (consider adding tests)
- **Visual regression**: Screenshot comparison for UI consistency

### ESP32 Testing

- **Hardware validation**: Test on actual ESP32 + display hardware
- **Touch calibration**: Verify touch input accuracy
- **Performance profiling**: Check frame rates and responsiveness
- **Power consumption**: Monitor for battery-powered scenarios

### UI/UX Testing

- **Layout responsiveness**: Test different screen sizes (desktop resizing, different displays)
- **Dark mode**: If supported, verify both light and dark themes
- **Accessibility**: Consider font sizes, contrast, touch target sizes
- **Animations**: Verify smooth animations don't cause lag

## Common Pitfalls

### Slint-Specific

- **Property binding loops**: Avoid circular dependencies in property bindings
- **Performance on ESP32**: Complex gradients, shadows may impact frame rate
- **Touch targets**: Ensure touch areas are large enough (minimum 44x44px)
- **Font sizes**: Readable on both desktop and small embedded displays
- **Color depth**: ESP32 displays may have limited color depth

### ESP32-Specific

- **Memory fragmentation**: ESP32 heap can fragment - monitor with `heap_caps_get_info()`
- **PSRAM usage**: If available, use PSRAM for framebuffers
- **Wi-Fi coexistence**: Display updates may be impacted by Wi-Fi activity
- **Flash wear**: Minimize writes to flash during normal operation
- **Bootup time**: Large UI resources increase startup time

### Desktop-Specific

- **Cross-platform testing**: Test on Linux, Windows, and macOS if targeting all
- **Packaging**: Consider packaging (AppImage, .exe, .dmg) for distribution
- **Auto-updates**: Plan for update mechanism if distributing widely

## Development Workflow

### 1. UI Development (Slint)
Create or modify `.slint` files in [ui/](ui/) directory:
- Design components using Slint declarative syntax
- Define properties and callbacks for C++ integration
- Use `@preview` mode for rapid iteration

### 2. View Model Implementation (C++)
Implement business logic in [common/src/view_models/](common/src/view_models/):
- Follow MVVM pattern: no UI code in view models
- Use ETL delegates for observer pattern callbacks
- Keep state management thread-safe with mutexes

### 3. Desktop Testing
Build and test on desktop for rapid iteration:
```bash
cmake -B build -S . -DGUI_BUILD_ESP32=OFF
cmake --build build
./build/desktop/gui_desktop
```
- Test UI interactions and state management
- Verify communication protocol with actual device or mock
- Debug with GDB/LLDB and standard C++ tools

### 4. Platform Backend Implementation
Implement [ConnectionBackend](common/include/gui_common/backends/connection_backend.h) interface:
- **Desktop**: [desktop/src/serial_backend.cpp](desktop/src/serial_backend.cpp) using CSerialPort
- **ESP32**: [esp32/main/uart_backend.cpp](esp32/main/uart_backend.cpp) using ESP-IDF UART driver
- Ensure callbacks are thread-safe for platform

### 5. Application Wiring
Connect view models, backend, and UI in main.cpp:
- Instantiate view models with backend dependency injection
- Register callbacks between layers
- Set up Slint property bindings and event handlers
- Example: [desktop/src/main.cpp](desktop/src/main.cpp#L170-L185)

### 6. ESP32 Deployment
Build and flash to hardware:
```bash
cd esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
- Test on actual display hardware
- Verify touch input and performance
- Monitor heap usage and frame rate

### 7. Optimization
Profile and optimize bottlenecks:
- **Desktop**: Use standard profiling tools (perf, Instruments, etc.)
- **ESP32**: Use ESP-IDF heap tracing and FreeRTOS task monitoring
- Optimize Slint UI (reduce bindings, simplify gradients)
- Optimize communication (batch messages, adjust frame rates)

## Key Features

### MIDI Note Assignment Workflow

Interactive workflow for assigning MIDI notes to PWM channels (desktop only):

1. **User initiates**: Clicks "Assign Note" button for a channel
2. **GUI enters assignment mode**:
   - `is_assigning_note` atomic flag set to true
   - `channel_awaiting_note` stores target channel index
   - UI shows "Waiting for MIDI note..." feedback
3. **User plays MIDI key**: Device forwards MIDI NoteOn/NoteOff to GUI
4. **GUI captures note**:
   - `OnMidiMessage` callback checks assignment mode
   - Extracts MIDI note number (0-127)
   - Converts to string (e.g., "C4", "A#3")
5. **GUI updates UI**:
   - Invokes `note_assigned_from_backend` callback on UI thread
   - UI updates channel configuration popup
   - Assignment mode exits
6. **User saves configuration**: Sends ChannelConfig to device

Implementation: [desktop/src/main.cpp](desktop/src/main.cpp#L26-L64)

### Connection State Machine

ConnectionViewModel implements a 4-state machine for robust connection handling:

```
┌─────────────┐
│ Disconnected│
└──────┬──────┘
       │ connect() called
       │ send HeartBeat(request_telemetry=true)
       ▼
┌──────────────────────────────┐
│ WaitingForHeartBeatResponse  │
└──────────┬───────────────────┘
           │ Response received (ACK)
           │ or HeartBeat echo received
           ▼
┌──────────────────────────┐
│ WaitingForTelemetryData  │◄──┐ Counting telemetry
└──────────┬───────────────┘   │ messages received
           │                    │
           │ All 16 channels    │
           │ telemetry received │
           ▼                    │
┌─────────────────┐             │
│ FullyConnected  │─────────────┘ Ongoing operation
└─────────────────┘

Connection lost at any point → Disconnected
```

**Benefits**:
- Prevents UI from showing stale data during initial sync
- Shows "Waiting for device data..." during telemetry download
- Clear separation between transport connected and application ready

Implementation: [common/src/view_models/connection_view_model.cpp](common/src/view_models/connection_view_model.cpp#L143-L170)

### Common Development Tasks

**Adding a new message type**:
1. Update libcomm FlatBuffers schema
2. Add handler in [MessageProcessor](common/src/message_processor.cpp)
3. Update relevant view model
4. Update Slint UI to display new data

**Adding a new view model**:
1. Create header in `common/include/gui_common/view_models/`
2. Implement in `common/src/view_models/`
3. Add to [common/CMakeLists.txt](common/CMakeLists.txt)
4. Wire into main.cpp on both platforms
5. Connect to Slint UI via callbacks

**Debugging connection issues**:
1. Enable verbose logging in [log.h](common/include/gui_common/log.h)
2. Monitor frame parser state
3. Verify CRC calculations
4. Check backend read/write operations
5. Use USB serial monitor in parallel to sniff traffic

## UI Design Guidelines

- **Visual hierarchy**: Use size, color, and spacing to guide attention
- **Consistent spacing**: Use multiples of 4px or 8px for spacing
- **Color palette**: Define consistent colors (consider theme support)
- **Typography**: Limit font sizes to 3-4 levels for hierarchy
- **Feedback**: Provide visual feedback for all interactive elements
- **Loading states**: Show loading indicators for async operations
- **Error states**: Clear error messages and recovery options

## Resources

- Slint documentation: https://slint.dev/docs
- Slint examples: https://github.com/slint-ui/slint/tree/master/examples
- ESP-IDF documentation: https://docs.espressif.com/projects/esp-idf/
- ESP32 Slint integration: Check managed_components/slint__slint
