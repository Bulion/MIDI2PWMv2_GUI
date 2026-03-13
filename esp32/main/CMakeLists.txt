set(GUI_UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ui")

idf_component_register(
    SRCS
        "src/main.cpp"
        "src/display_init.cpp"
        "src/ota_handler.cpp"
        "src/comm_task.cpp"
        "src/ui_callbacks.cpp"
        "src/ui_poll_timer.cpp"
        "src/waveshare_rgb_lcd_port.cpp"
        "src/uart_backend.cpp"
        "src/log_forwarder.cpp"
        "src/esp32_flash_writer.cpp"
        "src/psram_buffered_flash_writer.cpp"
        "src/usb_midi_host.cpp"
        "src/midi_bridge.cpp"
    PRIV_REQUIRES driver app_update esp_timer usb
    INCLUDE_DIRS
        "include"
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
idf_component_get_property(tusb_dir espressif__tinyusb COMPONENT_DIR)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tinyusb_config")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tinyusb_config")

target_sources(${tusb_lib} PRIVATE
    "${tusb_dir}/src/host/usbh.c"
    "${tusb_dir}/src/host/hub.c"
    "${tusb_dir}/src/class/midi/midi_host.c"
    "${tusb_dir}/src/portable/synopsys/dwc2/hcd_dwc2.c"
)
target_include_directories(${tusb_lib} PRIVATE "${tusb_dir}/src/host")
