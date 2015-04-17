TEMPLATE = app

QT += qml quick quickcl

SOURCES = histogram.cpp

RESOURCES = histogram.qrc

OTHER_FILES = $$PWD/qml/histogram.qml

osx {
    LIBS += -framework OpenCL
} else {
    LIBS += -lOpenCL
}
