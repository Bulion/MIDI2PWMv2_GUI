cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(MIDI2PWMv2_GUI)

set(GIT_DIR_LOOKUP_POLICY "ALLOW_LOOKING_ABOVE_CMAKE_SOURCE_DIR=FALSE")

macro(git_describe VAR_NAME)
    set(${VAR_NAME} "nogit")
endmacro()

find_program(FLATC_EXECUTABLE flatc REQUIRED)
set(LIBCOMM_FLATC_COMMAND ${FLATC_EXECUTABLE} CACHE FILEPATH "" FORCE)

set(LIBCOMM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBCOMM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBCOMM_ETL_NO_STL OFF CACHE BOOL "" FORCE)
set(LIBCOMM_ETL_TARGET_OS FREERTOS CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../libcomm ${CMAKE_BINARY_DIR}/libcomm)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/gui_common)

target_link_libraries(libcomm PUBLIC idf::freertos idf::log)
target_link_libraries(gui_common PUBLIC idf::log)

# ETL expects FreeRTOS headers without freertos/ prefix
target_include_directories(libcomm PUBLIC
    "$ENV{IDF_PATH}/components/freertos/FreeRTOS-Kernel/include/freertos"
)

target_compile_definitions(libcomm PUBLIC ESP_PLATFORM FLATBUFFERS_NO_ABSOLUTE_PATH_RESOLUTION)
target_compile_definitions(gui_common PUBLIC ESP_PLATFORM)
