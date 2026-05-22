cmake_minimum_required(VERSION 3.16)
project(FileTransfer LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automatic Qt processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network WebSockets)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Core
        Gui
        Widgets
        WebSockets
)
qt_standard_project_setup()

set(PROJECT_SOURCES
    main.cpp
    FileTransfer.ui
    FileTransfer.h
    FileTransfer.cpp
    LogHandler.h
    SocketIOClient.h
    SocketIOClient.cpp
    WebRTCEngine.h
    WebRTCEngine.cpp
    FileTransferManager.h
    FileTransferManager.cpp
    SignalingClient.h
    SignalingClient.cpp
    FileMitra.qrc
)

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::WebSockets
)

# ================================
# libdatachannel integration
# ================================
option(HAVE_LIBDATACHANNEL "Enable libdatachannel support" ON)

if(HAVE_LIBDATACHANNEL)
    set(LIBDATACHANNEL_ROOT "${CMAKE_SOURCE_DIR}/libdatachannel")

    # Prefer target-scoped include dir
    target_include_directories(${PROJECT_NAME}
        PUBLIC
            "${LIBDATACHANNEL_ROOT}/include"
    )

    # Define the macro for the compiler
    target_compile_definitions(${PROJECT_NAME}
        PUBLIC
            HAVE_LIBDATACHANNEL
    )

    # Link libraries (keep your generator-expr usage)
    set(LIBDATACHANNEL_DEBUG_LIB "${LIBDATACHANNEL_ROOT}/lib/Debug/datachannel.lib")
    set(LIBDATACHANNEL_RELEASE_LIB "${LIBDATACHANNEL_ROOT}/lib/Release/datachannel.lib")

    target_link_libraries(${PROJECT_NAME}
        PUBLIC
            $<$<CONFIG:Debug>:${LIBDATACHANNEL_DEBUG_LIB}>
            $<$<CONFIG:Release>:${LIBDATACHANNEL_RELEASE_LIB}>
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBDATACHANNEL_ROOT}/bin/$<CONFIG>/datachannel.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()