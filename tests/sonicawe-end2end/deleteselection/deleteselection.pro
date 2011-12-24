#-------------------------------------------------
#
# Project created by Johan 2011-12-09
#
#-------------------------------------------------

####################
# Compiler settings

QT += testlib
QT += opengl
QT += network

win32:CONFIG += debug_and_release

TEMPLATE = app
win32:TEMPLATE = vcapp


unix:QMAKE_CXXFLAGS_RELEASE += -fopenmp
unix:QMAKE_LFLAGS_RELEASE += -fopenmp
win32:QMAKE_CXXFLAGS_RELEASE += /openmp


####################
# Source code

SOURCES += *.cpp


####################
# Compiler flags

DEFINES += SRCDIR=\\\"$$PWD/\\\"

unix:IS64 = $$system(if [ "`uname -m`" = "x86_64" ]; then echo 64; fi)

INCLUDEPATH += \
    ../../../../../sonic/gpumisc \
    ../../../../../sonic/sonicawe \

win32 {
    INCLUDEPATH += \
        ../../../../../winlib/glut \
        ../../../../../winlib/glew/include \
        ../../../../../winlib \
    LIBS += \
        -l../../../../winlib/glut/glut32 \
        -l../../../../winlib/glew/lib/glew32 \

    LIBS += \
        -L../../../../sonicawe/debug -lsonicawe \
} else {
    # build sonicawe with qmake CONFIG+=testlib
    LIBS += -L../../../../sonicawe -lsonicawe \

    # find libsonicawe when executing from project path
    QMAKE_LFLAGS += -Wl,-rpath=../../../
}


####################
# Temporary output

TMPDIR=

usecuda {
  TMPDIR = $${TMPDIR}/cuda
} else:useopencl {
  TMPDIR = $${TMPDIR}/opencl
} else {
  TMPDIR = $${TMPDIR}/cpu
}

SONICAWEMOCDIR = tmp/lib/$${TMPDIR}
TMPDIR = tmp/$${TMPDIR}

win32:RCC_DIR = $${TMPDIR}
MOC_DIR = $${TMPDIR}
OBJECTS_DIR = $${TMPDIR}/
UI_DIR = $${TMPDIR}

INCLUDEPATH += ../../../../../sonic/sonicawe/$${SONICAWEMOCDIR}

CONFIG(debug, debug|release):OBJECTS_DIR = $${OBJECTS_DIR}debug/
else:OBJECTS_DIR = $${OBJECTS_DIR}release/


unix:!macx {
    QMAKE_CXX = g++-4.3
    QMAKE_CC = gcc-4.3
    QMAKE_LINK = g++-4.3
}

# #######################################################################
# CUDA
# #######################################################################
usecuda {
DEFINES += USE_CUDA

LIBS += -lcufft -lcudart -lcuda
CONFIG(debug, debug|release): CUDA_FLAGS += -g
CUDA_FLAGS += --use_fast_math
#CUDA_FLAGS += --ptxas-options=-v


CUDA_CXXFLAGS = $$QMAKE_CXXFLAGS
testlib:CUDA_CXXFLAGS += -fPIC
CONFIG(debug, debug|release):CUDA_CXXFLAGS += $$QMAKE_CXXFLAGS_DEBUG
else:CUDA_CXXFLAGS += $$QMAKE_CXXFLAGS_RELEASE
win32 {
    INCLUDEPATH += "$(CUDA_INC_PATH)"
    LIBS += -L"$(CUDA_LIB_PATH)"
    CUDA_CXXFLAGS -= -Zc:wchar_t-
    CUDA_CXXFLAGS += -Zc:wchar_t
    CUDA_CXXFLAGS += /EHsc
    cuda.output = $${OBJECTS_DIR}${QMAKE_FILE_BASE}_cuda.obj
    cuda.commands = \"$(CUDA_BIN_PATH)/nvcc.exe\" \
        -ccbin $${QMAKE_CC} \
        -c \
        -Xcompiler \
        \"$$join(CUDA_CXXFLAGS," ")\" \
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
        -ccbin $${QMAKE_CC} \
        -c \
        -Xcompiler \
        $$join(CUDA_CXXFLAGS,",") \
        $$join(INCLUDEPATH,'" -I "../../sonic/sonicawe/','-I "../../sonic/sonicawe/','"') \
        $$CUDA_FLAGS \
        ${QMAKE_FILE_NAME} \
        -o \
        ${QMAKE_FILE_OUT}
    cuda.dependcy_type = TYPE_C
    cuda.depend_command_dosntwork = nvcc \
        -M \
        -Xcompiler \
        $$join(CUDA_CXXFLAGS,",") \
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
        -ccbin $${QMAKE_CC} \
        -c \
        -Xcompiler \
        $$join(CUDA_CXXFLAGS,",") \
        $$join(INCLUDEPATH,'" -I "','-I "','"') \
        $$CUDA_FLAGS \
        ${QMAKE_FILE_NAME} \
        -o \
        ${QMAKE_FILE_OUT}
    cuda.dependcy_type = TYPE_C
}

cuda.input = CUDA_SOURCES
QMAKE_EXTRA_COMPILERS += cuda

} #usecuda