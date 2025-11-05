# ESP32-S3 GUI for MIDI2PWM

Embedded display GUI for the MIDI2PWM project, running on ESP32-S3 with a Waveshare 800x480 RGB LCD touchscreen.

## Overview

This is the embedded version of the MIDI2PWM GUI, designed to run directly on ESP32-S3 hardware with an integrated display and touch interface. It provides real-time monitoring and configuration of the STM32G473 MIDI2PWM firmware via UART communication.

### Key Features

- **800x480 RGB565 display** with hardware framebuffers
- **GT911 capacitive touch** interface (5-point multi-touch)
- **Real-time telemetry** from 16 PWM channels
- **MIDI message monitoring** with live updates
- **Channel configuration** UI (view and edit mode parameters)
- **Shared UI codebase** with desktop application (100% Slint UI reuse)
- **FreeRTOS-based** architecture with non-blocking UART communication

### Architecture

```
┌─────────────────────────────────────────────┐
│  ESP32-S3 Application                       │
├─────────────────────────────────────────────┤
│  Slint UI (app-window.slint)                │
│    ├─ Connection screen                     │
│    ├─ Channel cards (16x)                   │
│    ├─ Configuration popup                   │
│    └─ MIDI message display                  │
├─────────────────────────────────────────────┤
│  View Models (MVVM pattern)                 │
│    ├─ ConnectionViewModel                   │
│    ├─ ChannelTelemetryViewModel            │
│    └─ MidiMessageViewModel                  │
├─────────────────────────────────────────────┤
│  Platform Layer                             │
│    ├─ UartBackend (FreeRTOS task)          │
│    ├─ RGB LCD driver (ESP-IDF)             │
│    └─ Touch driver (GT911)                  │
├─────────────────────────────────────────────┤
│  libcomm Protocol                           │
│    ├─ Frame parser (COBS + CRC32)          │
│    ├─ FlatBuffers messages                  │
│    └─ Message processor                     │
└─────────────────────────────────────────────┘
         │ UART (921600 baud)
         ▼
┌─────────────────────────────────────────────┐
│  STM32G473 MIDI2PWM Firmware                │
│    ├─ MIDI input processing                 │
│    ├─ 16-channel PWM output                 │
│    ├─ libcomm protocol                      │
│    └─ USB CDC + MIDI device                 │
└─────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- ESP-IDF v5.0 or later (v5.5 recommended)
- ESP32-S3 module with PSRAM
- Waveshare 800x480 RGB LCD display + GT911 touch
- USB cable for programming

### Build and Flash

```bash
# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Navigate to project directory
cd GUI/esp32

# Build firmware
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

For detailed instructions, see [ESP32_DEPLOYMENT.md](ESP32_DEPLOYMENT.md).

## Documentation

### Setup and Deployment
- **[ESP32_HARDWARE_SETUP.md](ESP32_HARDWARE_SETUP.md)** - Pin assignments, wiring diagrams, and hardware configuration
- **[ESP32_DEPLOYMENT.md](ESP32_DEPLOYMENT.md)** - Build system, flashing procedures, and deployment guide
- **[ESP32_TESTING_CHECKLIST.md](ESP32_TESTING_CHECKLIST.md)** - Comprehensive validation and testing procedures

### Additional Resources
- [Slint Documentation](https://slint.dev/docs/cpp/) - UI framework reference
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) - ESP32-S3 SDK
- [libcomm README](../../libcomm/README.md) - Communication protocol specification

## Hardware Requirements

### Minimum Configuration
- **MCU:** ESP32-S3 with PSRAM (8MB+ recommended)
- **Display:** 800x480 RGB LCD (RGB565, 16-bit parallel interface)
- **Touch:** GT911 capacitive touch controller (I2C)
- **Connection:** UART to STM32G473 (TX/RX/GND)
- **Power:** 3.3V @ 1A minimum

### Verified Hardware
- ESP32-S3-WROOM-1 module (8MB PSRAM)
- Waveshare 4.3" 800x480 RGB LCD
- GT911 5-point capacitive touch panel

See [ESP32_HARDWARE_SETUP.md](ESP32_HARDWARE_SETUP.md) for complete pin assignments.

## Memory Configuration

### PSRAM (External RAM)
- **Size:** 8MB Octal SPI PSRAM @ 120 MHz
- **Usage:**
  - Framebuffers: 2x 768KB = 1.5MB
  - Slint widgets: ~500KB
  - Free: ~6MB

### Internal RAM
- **Main task stack:** 64KB
- **UART reader task stack:** 4KB
- **Heap usage:** ~100KB
- **Free:** ~50KB

### Flash
- **Application size:** ~2MB
- **Total required:** ~3MB (app + partitions)

## Performance Characteristics

### Measured Performance
- **UI frame rate:** 10-30 FPS (target: 10+ FPS)
- **Touch latency:** <100ms (typical)
- **UART message latency:** <50ms (typical)
- **Boot time:** ~3-5 seconds to UI ready

### Resource Usage
- **CPU utilization:** ~40-60% (during active UI updates)
- **PSRAM bandwidth:** ~50MB/s (framebuffer rendering)
- **UART throughput:** ~100 messages/second max

## Development Status

### Implemented Features
✅ RGB LCD display driver (800x480, RGB565)
✅ GT911 touch controller driver
✅ Slint UI integration with double buffering
✅ UART communication with STM32 (921600 baud)
✅ libcomm protocol (COBS + CRC32 + FlatBuffers)
✅ Real-time telemetry display (16 channels)
✅ MIDI message monitoring
✅ Connection state machine
✅ Shared UI with desktop application

### Not Yet Implemented
❌ Configuration editing (read-only currently)
❌ WiFi connectivity
❌ OTA firmware updates
❌ Persistent settings storage (NVS)
❌ Backlight control
❌ Screen rotation
❌ Power management/sleep modes

### Known Limitations
- Touch calibration not exposed in UI
- Console UART conflicts with STM32 communication
- No error recovery for corrupted frames (connection reset only)
- Fixed orientation (no rotation support)

## Troubleshooting

### Display Issues
**Symptom:** Blank screen
- Check PSRAM initialization logs
- Verify RGB data line connections (all 16 pins)
- Measure pixel clock (should be 16 MHz)
- Check framebuffer allocation success

**Symptom:** Visual artifacts or tearing
- Adjust RGB timing parameters in [waveshare_rgb_lcd_port.cpp](main/waveshare_rgb_lcd_port.cpp)
- Reduce pixel clock if signal integrity issues
- Check PSRAM speed configuration

### Touch Issues
**Symptom:** No touch response
- Verify I2C pull-up resistors present
- Check GT911 power supply
- Monitor I2C transactions with logic analyzer
- Verify touch controller I2C address

### Communication Issues
**Symptom:** Cannot connect to STM32
- Verify TX/RX crossover (not straight-through)
- Check baud rate matches (921600 on both sides)
- Ensure common ground between ESP32 and STM32
- Test UART loopback (TX→RX on same device)

For more troubleshooting tips, see [ESP32_DEPLOYMENT.md](ESP32_DEPLOYMENT.md#troubleshooting).

## Contributing

### Code Structure
```
esp32/
├── main/
│   ├── main.cpp                    # Application entry point
│   ├── uart_backend.{cpp,h}        # UART driver (FreeRTOS task)
│   ├── waveshare_rgb_lcd_port.{cpp,h}  # Display driver
│   ├── CMakeLists.txt              # Component build config
│   └── idf_component.yml           # Managed dependencies
├── CMakeLists.txt                  # Project build config
├── sdkconfig                       # ESP-IDF configuration
├── partitions.csv                  # Flash partition table
└── README.md                       # This file
```

### Shared Components
- `../common/` - Shared view models and logic (MVVM)
- `../ui/` - Slint UI files (shared with desktop)
- `../../libcomm/` - Communication protocol library

### Development Workflow
1. Make changes to code
2. Build: `idf.py build`
3. Flash: `idf.py -p /dev/ttyUSB0 flash`
4. Monitor: `idf.py -p /dev/ttyUSB0 monitor`
5. Test according to [ESP32_TESTING_CHECKLIST.md](ESP32_TESTING_CHECKLIST.md)

### Testing Requirements
All changes must:
- Build without warnings
- Pass hardware validation checklist
- Maintain stable memory usage (no leaks)
- Not degrade UI performance (<10 FPS)

## License

This project is part of the MIDI2PWM system. See top-level LICENSE file for details.

## See Also

- [Desktop GUI](../desktop/) - Desktop version of the GUI
- [STM32 Firmware](../../Software/) - MIDI2PWM firmware for STM32G473
- [libcomm](../../libcomm/) - Shared communication protocol library
- [Project Status](../../PROJECT_STATUS.md) - Overall project status and roadmap
