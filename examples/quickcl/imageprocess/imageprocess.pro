TEMPLATE = app

QT += qml quick quickcl

SOURCES = imageprocess.cpp

RESOURCES = imageprocess.qrc

OTHER_FILES = $$PWD/qml/imageprocess.qml

osx {
    LIBS += -framework OpenCL
} else {
    LIBS += -lOpenCL
}
