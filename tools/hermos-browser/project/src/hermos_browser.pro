TEMPLATE = app
TARGET   = hermos_browser
QT       = core gui widgets network
CONFIG  += c++17
SOURCES += main.cpp
HEADERS += shield.h smartbar.h theme.h
RESOURCES += hermos_browser.qrc
LIBS    += -l:libQt6WebEngineCore.lib.so -l:libQt6WebEngineWidgets.lib.so
