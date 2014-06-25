# This builds a static library
# Use Makefile.unittest to build and run a unit test

TARGET = backtrace
TEMPLATE = lib
win32:TEMPLATE = vclib
win32:CONFIG += debug_and_release

CONFIG += staticlib warn_on
CONFIG += c++11 buildflags
CONFIG += tmpdir

QT += opengl

SOURCES += *.cpp
HEADERS += *.h

win32: INCLUDEPATH += ../sonicawe-winlib

macx:exists(/opt/local/include/): INCLUDEPATH += /opt/local/include/ # macports
macx:exists(/usr/local/include/): INCLUDEPATH += /usr/local/include/ # homebrew

OTHER_FILES += \
    LICENSE \
    *.pro \

win32 { 
    othersources.input = OTHER_FILES
    othersources.output = ${QMAKE_FILE_NAME}
    QMAKE_EXTRA_COMPILERS += othersources
}
