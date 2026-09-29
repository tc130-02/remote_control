cmake_minimum_required(VERSION 3.10)

project(remote_control_windows)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(win_server server.cpp)
add_executable(win_client client.cpp)

target_include_directories(win_server PRIVATE ../common)
target_include_directories(win_client PRIVATE ../common)

target_link_libraries(win_server ws2_32 gdi32 user32)
target_link_libraries(win_client ws2_32 gdi32 user32)

option(BUILD_QT_CLIENT "Build the Qt connection-management client" OFF)
option(BUILD_QT_SERVER "Build the Qt server-management interface" OFF)
option(BUILD_QT_APP "Build the unified Qt remote-control application" OFF)

if(BUILD_QT_CLIENT OR BUILD_QT_SERVER OR BUILD_QT_APP)
    find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Widgets)

    if(COMMAND qt_standard_project_setup)
        qt_standard_project_setup()
    else()
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTOUIC ON)
        set(CMAKE_AUTORCC ON)
    endif()

    if(MSVC)
        add_compile_options(/utf-8)
    endif()
endif()

if(BUILD_QT_APP)
    qt_add_executable(remote_control WIN32
        ../qt_app/main.cpp
        ../qt_app/main_window.cpp
        ../qt_app/main_window.h
        qt_client/connection_window.cpp
        qt_client/connection_window.h
        qt_client/remote_screen_widget.cpp
        qt_client/remote_screen_widget.h
        ../qt_server/server_window.cpp
        ../qt_server/server_window.h
    )

    target_include_directories(
        remote_control
        PRIVATE ../common qt_client ../qt_app ../qt_server
    )
    target_link_libraries(
        remote_control
        PRIVATE Qt6::Concurrent Qt6::Network Qt6::Widgets
    )
    add_dependencies(remote_control win_server)
endif()

if(BUILD_QT_CLIENT)
    qt_add_executable(qt_client WIN32
        qt_client/main.cpp
        qt_client/connection_window.cpp
        qt_client/connection_window.h
        qt_client/remote_screen_widget.cpp
        qt_client/remote_screen_widget.h
    )

    target_include_directories(qt_client PRIVATE ../common)
    target_link_libraries(
        qt_client
        PRIVATE Qt6::Concurrent Qt6::Network Qt6::Widgets
    )
endif()

if(BUILD_QT_SERVER)
    qt_add_executable(qt_server WIN32
        ../qt_server/main.cpp
        ../qt_server/server_window.cpp
        ../qt_server/server_window.h
    )

    target_link_libraries(qt_server PRIVATE Qt6::Widgets)
    add_dependencies(qt_server win_server)
endif()
