QT += core gui widgets network

CONFIG += c++17
TEMPLATE = app
TARGET = NetTransferTool

SOURCES += \
    src/filetransferclient.cpp \
    src/filetransferserver.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/protocol.cpp

HEADERS += \
    src/filetransferclient.h \
    src/filetransferserver.h \
    src/mainwindow.h \
    src/protocol.h
