add_library(gui_common STATIC
    src/comm_loop.cpp
    src/message_processor.cpp
    src/view_models/channel_telemetry_view_model.cpp
    src/view_models/connection_view_model.cpp
    src/view_models/midi_message_view_model.cpp
)

target_include_directories(gui_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gui_common PUBLIC cxx_std_20)

target_link_libraries(gui_common
    PUBLIC
        libcomm
)
