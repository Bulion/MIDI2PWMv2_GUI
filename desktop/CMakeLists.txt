cmake_minimum_required(VERSION 3.24)

include(FetchContent)
find_package(Threads REQUIRED)

set(SLINT_FETCH_VERSION v1.12.1 CACHE STRING "Slint version for desktop build")

if(NOT TARGET Slint::Slint)
    FetchContent_Declare(slint
        GIT_REPOSITORY https://github.com/slint-ui/slint.git
        GIT_TAG        ${SLINT_FETCH_VERSION}
    )
    FetchContent_MakeAvailable(slint)
endif()

if(NOT TARGET cserialport)
    FetchContent_Declare(cserialport
        GIT_REPOSITORY https://github.com/itas109/CSerialPort.git
        GIT_TAG        v4.3.1
    )
    set(CSerialPort_VERSION_MAJOR 4)
    set(CSerialPort_VERSION_MINOR 3)
    set(CSerialPort_VERSION_PATCH 1)
    set(CSerialPort_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CSerialPort_BUILD_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(cserialport)
endif()

add_executable(gui_desktop
    src/main.cpp
    src/serial_backend.cpp
)

target_compile_features(gui_desktop PRIVATE cxx_std_20)

target_include_directories(gui_desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui
    ${cserialport_SOURCE_DIR}/include
)

slint_target_sources(gui_desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/app-window.slint
)

target_link_libraries(gui_desktop PRIVATE
    Slint::Slint
    gui_common
    libcserialport
    Threads::Threads
)
