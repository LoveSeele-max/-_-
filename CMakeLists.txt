cmake_minimum_required(VERSION 3.16)

project(NetTransferTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(NetTransferTool
    src/filetransferclient.cpp
    src/filetransferserver.cpp
    src/main.cpp
    src/mainwindow.cpp
    src/protocol.cpp
    src/filetransferclient.h
    src/filetransferserver.h
    src/mainwindow.h
    src/protocol.h
)

target_link_libraries(NetTransferTool PRIVATE Qt6::Widgets Qt6::Network)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(NetTransferTool)
endif()
