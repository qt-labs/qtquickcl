SOURCES += main.cpp
CONFIG -= qt
CONFIG += console

osx {
    LIBS += -framework OpenCL
    DEFINES += Q_OS_OSX
}

unix: !osx: LIBS += -lOpenCL

win32:!winrt:!wince*: LIBS += -lopengl32 -lOpenCL
