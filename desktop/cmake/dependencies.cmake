include(FetchContent)

if(NOT DEFINED SLINT_VERSION)
    set(SLINT_VERSION "1.15.0")
endif()

if(NOT TARGET Slint::Slint)
    message(STATUS "Fetching Slint v${SLINT_VERSION} for desktop")
    FetchContent_Declare(slint
        GIT_REPOSITORY https://github.com/slint-ui/slint.git
        GIT_TAG        v${SLINT_VERSION}
    )
    FetchContent_MakeAvailable(slint)
endif()

if(NOT TARGET cserialport)
    message(STATUS "Fetching CSerialPort v4.3.1")
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

if(NOT TARGET libcomm)
    message(STATUS "Adding libcomm for desktop build")
    set(LIBCOMM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIBCOMM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBCOMM_ETL_NO_STL OFF CACHE BOOL "" FORCE)
    set(LIBCOMM_ETL_TARGET_OS NONE CACHE STRING "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../libcomm ${CMAKE_BINARY_DIR}/libcomm)
endif()
