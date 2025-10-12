cmake_minimum_required(VERSION 3.24)

include(FetchContent)

set(SLINT_FETCH_VERSION v1.12.1 CACHE STRING "Slint version for desktop build")

if(NOT TARGET Slint::Slint)
    FetchContent_Declare(slint
        GIT_REPOSITORY https://github.com/slint-ui/slint.git
        GIT_TAG        ${SLINT_FETCH_VERSION}
    )
    FetchContent_MakeAvailable(slint)
endif()

add_executable(gui_desktop
    src/main.cpp
)

target_compile_features(gui_desktop PRIVATE cxx_std_20)

target_include_directories(gui_desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ui)

slint_target_sources(gui_desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/app-window.slint
)

target_link_libraries(gui_desktop PRIVATE Slint::Slint)
