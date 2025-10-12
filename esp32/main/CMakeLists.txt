idf_component_register(
    SRCS 
        "main.cpp"
        "waveshare_rgb_lcd_port.cpp"
    PRIV_REQUIRES driver usb
    INCLUDE_DIRS 
        "."
    REQUIRES
        slint
        tinyusb
)

set(GUI_UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ui")

slint_target_sources(
    ${COMPONENT_LIB}
        ${GUI_UI_DIR}/app-window.slint
)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(tinyusb_src_dir espressif__tinyusb COMPONENT_DIR)
target_sources(
    ${tusb_lib} 
        PRIVATE
            "${tinyusb_src_dir}/src/portable/synopsys/dwc2/hcd_dwc2.c"
            "${tinyusb_src_dir}/src/host/usbh.c"
            "${tinyusb_src_dir}/src/host/hub.c"
            "${tinyusb_src_dir}/src/class/midi/midi_host.c"
            "${tinyusb_src_dir}/src/class/vendor/vendor_host.c"
)
target_include_directories(${tusb_lib} PUBLIC tinyusb_config)

    
