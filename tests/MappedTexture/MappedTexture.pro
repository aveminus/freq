#-------------------------------------------------
#
# Project created by QtCreator 2011-03-07T20:41:08
#
#-------------------------------------------------

QT       += testlib
QT += opengl

TARGET = tst_mappedvbotest
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += *.cpp
DEFINES += SRCDIR=\\\"$$PWD/\\\"

unix:IS64 = $$system(if [ "`uname -m`" = "x86_64" ]; then echo 64; fi)

INCLUDEPATH += \
    ../../../../sonic/gpumisc \
    ../../../../sonic/sonicawe \

LIBS = \
    -lGLEW \
    -lGLU \
    -lGL \
    -lglut \
    -L../../../gpumisc -lgpumisc \
#    -L../../../sonicawe -lsonicawe

CUDA_SOURCES += \
    *.cu

# "Other files" for Qt Creator
OTHER_FILES += \
    $$CUDA_SOURCES \

####################
# Temporary output

win32:RCC_DIR = tmp
MOC_DIR = tmp
OBJECTS_DIR = tmp/
UI_DIR = tmp

CONFIG(debug, debug|release):OBJECTS_DIR = tmp/debug/
else:OBJECTS_DIR = tmp/release/

# #######################################################################
# CUDA
# #######################################################################

LIBS += -lcufft -lcudart -lcuda
CONFIG(debug, debug|release): CUDA_FLAGS += -g
CUDA_FLAGS += --use_fast_math


win32 {
    INCLUDEPATH += "$(CUDA_INC_PATH)"
    LIBS += -L"$(CUDA_LIB_PATH)"
    QMAKE_CXXFLAGS -= -Zc:wchar_t-
    QMAKE_CXXFLAGS += -Zc:wchar_t
    cuda.output = $$OBJECTS_DIR/${QMAKE_FILE_BASE}_cuda.obj
    cuda.commands = \"$(CUDA_BIN_PATH)/nvcc.exe\" \
        -c \
        -Xcompiler \
        \"$$join(QMAKE_CXXFLAGS," ")\" \
        $$join(INCLUDEPATH,'" -I "','-I "','"') \
        $$CUDA_FLAGS \
        "${QMAKE_FILE_NAME}" \
        -o \
        "${QMAKE_FILE_OUT}"
}
unix:!macx {
    # auto-detect CUDA path
    # CUDA_DIR = $$system(which nvcc | sed 's,/bin/nvcc$,,')
    CUDA_DIR = /usr/local/cuda
    INCLUDEPATH += $$CUDA_DIR/include
    QMAKE_LIBDIR += $$CUDA_DIR/lib$$IS64
    cuda.output = $${OBJECTS_DIR}${QMAKE_FILE_BASE}_cuda.o
    cuda.commands = $${CUDA_DIR}/bin/nvcc \
        -c \
        -Xcompiler \
        $$join(QMAKE_CXXFLAGS,",") \
        $$join(INCLUDEPATH,'" -I "../../../../sonic/sonicawe/tests/MappedVbo/','-I "../../../../sonic/sonicawe/tests/MappedVbo/','"') \
        $$CUDA_FLAGS \
        ${QMAKE_FILE_NAME} \
        -o \
        ${QMAKE_FILE_OUT}
    cuda.dependcy_type = TYPE_C
    cuda.depend_command_dosntwork = nvcc \
        -M \
        -Xcompiler \
        $$join(QMAKE_CXXFLAGS,",") \
        $$join(INCLUDEPATH,'" -I "','-I "','"') \
        ${QMAKE_FILE_NAME} \
        | \
        sed \
        "s,^.*: ,," \
        | \
        sed \
        "s,^ *,," \
        | \
        tr \
        -d \
        '\\\n'
}

# cuda.depends = nvcc -M -Xcompiler $$join(QMAKE_CXXFLAGS,",") $$join(INCLUDEPATH,'" -I "','-I "','"') ${QMAKE_FILE_NAME} | sed "s,^.*: ,," | sed "s,^ *,," | tr -d '\\\n'

macx {
    # auto-detect CUDA path
    # CUDA_DIR = $$system(which nvcc | sed 's,/bin/nvcc$,,')
    # manual
    CUDA_DIR = /usr/local/cuda
    INCLUDEPATH += $$CUDA_DIR/include
    QMAKE_LIBDIR += $$CUDA_DIR/lib
    cuda.output = $${OBJECTS_DIR}${QMAKE_FILE_BASE}_cuda.o
    cuda.commands = $${CUDA_DIR}/bin/nvcc \
        -c \
        -Xcompiler \
        $$join(QMAKE_CXXFLAGS,",") \
        $$join(INCLUDEPATH,'" -I "','-I "','"') \
        $$CUDA_FLAGS \
        ${QMAKE_FILE_NAME} \
        -o \
        ${QMAKE_FILE_OUT}
    cuda.dependcy_type = TYPE_C
}

cuda.input = CUDA_SOURCES
QMAKE_EXTRA_COMPILERS += cuda
# end of cuda section #######################################################################
