cmake_minimum_required(VERSION 3.24)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(dependencies)

find_package(Threads REQUIRED)

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
    libcomm
    libcserialport
    Threads::Threads
)
