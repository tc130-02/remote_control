cmake_minimum_required(VERSION 3.16)

project(remote_control_linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

add_executable(linux_server server.cpp)
add_executable(linux_client client.cpp)

target_include_directories(
    linux_server
    PRIVATE ../common ${X11_INCLUDE_DIR}
)
target_include_directories(
    linux_client
    PRIVATE ../common ${X11_INCLUDE_DIR}
)

target_link_libraries(
    linux_server
    PRIVATE ${X11_LIBRARIES} ${X11_Xext_LIB} Threads::Threads
)
target_link_libraries(
    linux_client
    PRIVATE ${X11_LIBRARIES} Threads::Threads
)

option(BUILD_QT_CLIENT "Build the Qt remote-control client" OFF)
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
endif()

if(BUILD_QT_APP)
    qt_add_executable(remote_control
        ../qt_app/main.cpp
        ../qt_app/main_window.cpp
        ../qt_app/main_window.h
        ../windows/qt_client/connection_window.cpp
        ../windows/qt_client/connection_window.h
        ../windows/qt_client/remote_screen_widget.cpp
        ../windows/qt_client/remote_screen_widget.h
        ../qt_server/server_window.cpp
        ../qt_server/server_window.h
    )

    target_include_directories(
        remote_control
        PRIVATE
            ../common
            ../qt_app
            ../qt_server
            ../windows/qt_client
    )
    target_link_libraries(
        remote_control
        PRIVATE Qt6::Concurrent Qt6::Network Qt6::Widgets
    )
    add_dependencies(remote_control linux_server)
endif()

if(BUILD_QT_CLIENT)
    qt_add_executable(qt_client
        ../windows/qt_client/main.cpp
        ../windows/qt_client/connection_window.cpp
        ../windows/qt_client/connection_window.h
        ../windows/qt_client/remote_screen_widget.cpp
        ../windows/qt_client/remote_screen_widget.h
    )

    target_include_directories(
        qt_client
        PRIVATE ../common ../windows/qt_client
    )
    target_link_libraries(
        qt_client
        PRIVATE Qt6::Concurrent Qt6::Network Qt6::Widgets
    )
endif()

if(BUILD_QT_SERVER)
    qt_add_executable(qt_server
        ../qt_server/main.cpp
        ../qt_server/server_window.cpp
        ../qt_server/server_window.h
    )

    target_link_libraries(qt_server PRIVATE Qt6::Widgets)
    add_dependencies(qt_server linux_server)
endif()
