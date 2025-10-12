cmake_minimum_required(VERSION 3.24)
project(MIDI2PWMv2_GUI LANGUAGES CXX)

option(GUI_BUILD_DESKTOP "Build native desktop viewer" ON)
option(GUI_BUILD_ESP32 "Enable ESP32 firmware build (invokes idf.py)" ON)

if(GUI_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(GUI_BUILD_ESP32)
    find_program(IDF_PY_EXECUTABLE idf.py)
    if(IDF_PY_EXECUTABLE)
        add_custom_target(gui_esp32 ALL
            COMMAND ${IDF_PY_EXECUTABLE} build
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/esp32
            COMMENT "Building ESP32 firmware via idf.py"
        )
    else()
        message(WARNING "idf.py not found in PATH. ESP32 firmware target will be unavailable.")
    endif()
endif()
