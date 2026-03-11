set(GUI_UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ui")

idf_component_register(
    SRCS
        "main.cpp"
        "waveshare_rgb_lcd_port.cpp"
        "uart_backend.cpp"
        "log_forwarder.cpp"
        "esp32_flash_writer.cpp"
    PRIV_REQUIRES driver
    INCLUDE_DIRS
        "."
    REQUIRES
        slint
)

target_link_libraries(${COMPONENT_LIB} PRIVATE gui_common libcomm)

slint_target_sources(${COMPONENT_LIB} ${GUI_UI_DIR}/app-window.slint)

# Fix Slint compiler depfile: it writes absolute paths but ninja expects relative.
# This causes full rebuilds on every build. Patch the depfile to use paths relative
# to the build directory so ninja can properly track dependencies.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/app-window.h
    APPEND
    COMMAND ${CMAKE_COMMAND}
        -DDEPFILE=${CMAKE_CURRENT_BINARY_DIR}/app-window.d
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_CURRENT_LIST_DIR}/fix_depfile.cmake
)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tinyusb_config")
