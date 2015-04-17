TEMPLATE = app

QT += qml quick quickcl

SOURCES = particles.cpp

RESOURCES = particles.qrc

OTHER_FILES = $$PWD/qml/particles.qml

osx {
    LIBS += -framework OpenCL
} else {
    LIBS += -lOpenCL
}
