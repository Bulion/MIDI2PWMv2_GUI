set(GUI_UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ui")

idf_component_register(
    SRCS
        "main.cpp"
        "waveshare_rgb_lcd_port.cpp"
        "uart_backend.cpp"
        "log_forwarder.cpp"
    PRIV_REQUIRES driver
    INCLUDE_DIRS
        "."
    REQUIRES
        slint
)

target_link_libraries(${COMPONENT_LIB} PRIVATE gui_common libcomm)

slint_target_sources(${COMPONENT_LIB} ${GUI_UI_DIR}/app-window.slint)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tinyusb_config")
