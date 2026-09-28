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

if(BUILD_QT_CLIENT)
    find_package(Qt6 REQUIRED COMPONENTS Network Widgets)

    qt_standard_project_setup()
    qt_add_executable(qt_client WIN32
        qt_client/main.cpp
        qt_client/connection_window.cpp
        qt_client/connection_window.h
    )

    target_include_directories(qt_client PRIVATE ../common)
    target_link_libraries(qt_client PRIVATE Qt6::Network Qt6::Widgets)
endif()
