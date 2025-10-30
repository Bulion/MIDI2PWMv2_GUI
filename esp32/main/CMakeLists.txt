set(LIBCOMM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../libcomm ${CMAKE_BINARY_DIR}/libcomm-esp32)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../common ${CMAKE_BINARY_DIR}/gui_common-esp32)

idf_component_register(
    SRCS
        "main.cpp"
        "waveshare_rgb_lcd_port.cpp"
        "uart_backend.cpp"
    PRIV_REQUIRES driver
    INCLUDE_DIRS
        "."
    REQUIRES
        slint
)

set(GUI_UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../ui")

slint_target_sources(
    ${COMPONENT_LIB}
        ${GUI_UI_DIR}/app-window.slint
)

target_link_libraries(${COMPONENT_LIB} PRIVATE gui_common)

    
