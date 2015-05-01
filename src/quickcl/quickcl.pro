TARGET     = QtQuickCL
QT         = core core-private gui gui-private qml-private quick quick-private

load(qt_module)

DEFINES += QT_BUILD_QUICKCL_LIB

HEADERS = \
    qtquickclglobal.h \
    qquickclcontext.h \
    qquickclitem.h \
    qquickclrunnable.h \
    qquickclimagerunnable.h

SOURCES = \
    qquickclcontext.cpp \
    qquickclitem.cpp \
    qquickclimagerunnable.cpp

QMAKE_DOCS = $$PWD/doc/qtquickcl.qdocconf

osx: LIBS += -framework OpenCL
unix: !osx: LIBS += -lOpenCL
win32: !winrt: !wince*: LIBS += -lopengl32 -lOpenCL
