cmake_minimum_required(VERSION 3.24)
project(MIDI2PWMv2_GUI VERSION 1.0.0 LANGUAGES CXX)

set(SLINT_VERSION "1.15.0" CACHE STRING "Slint version for all platforms")

option(GUI_BUILD_DESKTOP "Build native desktop viewer" ON)
option(GUI_BUILD_ESP32 "Enable ESP32 firmware build (invokes idf.py)" ON)

add_subdirectory(common)

if(GUI_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(GUI_BUILD_ESP32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/esp32/main/idf_component.yml.in
        ${CMAKE_CURRENT_SOURCE_DIR}/esp32/main/idf_component.yml
        @ONLY
    )

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
