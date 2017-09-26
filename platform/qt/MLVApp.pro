#-------------------------------------------------
#
# Project created by QtCreator 2017-07-12T15:09:32
#
#-------------------------------------------------

QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

win32: TARGET = "MLVApp"
linux-g++*: TARGET = "mlvapp"
osx: TARGET = "MLV App"
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

##############
# Silent Mode, deactivate for more debug info
##############
DEFINES += STDOUT_SILENT

##############
# Compiler flags
##############
# Windows
win32: QMAKE_CFLAGS_DEBUG += -O3 -msse4.1 -mssse3 -msse3 -msse2 -msse -D_FILE_OFFSET_BITS=64 -std=c99
win32: QMAKE_CFLAGS_RELEASE += -O3 -msse4.1 -mssse3 -msse3 -msse2 -msse -D_FILE_OFFSET_BITS=64 -std=c99

# Linux
linux-g++*: QMAKE_CFLAGS_DEBUG += -O3 -msse4.1 -mssse3 -msse3 -msse2 -msse -std=c99
linux-g++*: QMAKE_CFLAGS_RELEASE += -O3 -msse4.1 -mssse3 -msse3 -msse2 -msse -std=c99

##############
#libpng: unchecking Add build library search path to DYLD_LIBRARY_PATH and DYLD_FRAMEWORK_PATH checkbox in my project Run section!!!
##############
#OSX: unzip qt/libpng16/lpng1631.zip (libpng 1.6.31) before, ./configure, make check, make install
macx: INCLUDEPATH += /usr/local/include/libpng16/
macx: LIBS += -L/usr/local/lib/ -lz -lpng

#Win: is precompiled with mingw32, so should work out of the box
win32: INCLUDEPATH += libpng16/include/
win32: LIBS += -L..\qt\libpng16\lib -llibpng \
               -L..\qt\zlib -lzlib1

#Linux (opensuse)
linux-g++*: INCLUDEPATH += /usr/local/include/
linux-g++*: LIBS += -L/usr/local/lib/ -lz -lpng
#Linux (Ubuntu)
#linux-g++*: INCLUDEPATH += /usr/local/include/libpng16/
#linux-g++*: LIBS += -L/usr/local/lib/ -lz -lpng16
##############

SOURCES += \
        main.cpp \
        MainWindow.cpp \
    ../../src/debayer/amaze_demosaic.c \
    ../../src/debayer/debayer.c \
    ../../src/matrix/matrix.c \
    ../../src/mlv/camera_matrices.c \
    ../../src/mlv/frame_caching.c \
    ../../src/mlv/video_mlv.c \
    ../../src/mlv/liblj92/lj92.c \
    ../../src/mlv/llrawproc/llrawproc.c \
    ../../src/mlv/llrawproc/pixelproc.c \
    ../../src/mlv/llrawproc/stripes.c \
    ../../src/mlv/llrawproc/patternnoise.c \
    ../../src/mlv/llrawproc/chroma_smooth.c \
    ../../src/mlv/llrawproc/hist.c \
    ../../src/processing/processing.c \
    ../../src/processing/raw_processing.c \
    InfoDialog.cpp \
    StatusDialog.cpp \
    Histogram.cpp \
    WaveFormMonitor.cpp \
    ExportSettingsDialog.cpp \
    ReceiptSettings.cpp \
    EditSliderValueDialog.cpp \
    DoubleClickLabel.cpp \
    ../../src/mlv/audio_mlv.c \
    AudioWave.cpp \
    ResizeLabel.cpp \
    GraphicsZoomView.cpp

HEADERS += \
        MainWindow.h \
    ../../src/debayer/debayer.h \
    ../../src/debayer/helpersse2.h \
    ../../src/matrix/matrix.h \
    ../../src/mlv/mlv.h \
    ../../src/mlv/mlv_object.h \
    ../../src/mlv/raw.h \
    ../../src/mlv/video_mlv.h \
    ../../src/mlv/liblj92/lj92.h \
    ../../src/mlv/llrawproc/llrawproc_object.h \
    ../../src/mlv/llrawproc/llrawproc.h \
    ../../src/mlv/llrawproc/pixelproc.h \
    ../../src/mlv/llrawproc/stripes.h \
    ../../src/mlv/llrawproc/patternnoise.h \
    ../../src/mlv/llrawproc/hist.h \
    ../../src/mlv/llrawproc/opt_med.h \
    ../../src/mlv/llrawproc/wirth.h \
    ../../src/processing/processing_object.h \
    ../../src/processing/raw_processing.h \
    ../../src/mlv_include.h \
    InfoDialog.h \
    MyApplication.h \
    StatusDialog.h \
    SystemMemory.h \
    Histogram.h \
    WaveFormMonitor.h \
    ExportSettingsDialog.h \
    ReceiptSettings.h \
    EditSliderValueDialog.h \
    DoubleClickLabel.h \
    AudioWave.h \
    ResizeLabel.h \
    GraphicsZoomView.h \
    DarkStyle.h

FORMS += \
        MainWindow.ui \
    InfoDialog.ui \
    StatusDialog.ui \
    ExportSettingsDialog.ui \
    EditSliderValueDialog.ui

RESOURCES += \
    ressources.qrc \
    darkstyle.qrc

DISTFILES += \
    Info.plist \
    MLVAPP.ico \
    darkstyle/icon_branch_closed.png \
    darkstyle/icon_branch_end.png \
    darkstyle/icon_branch_more.png \
    darkstyle/icon_branch_open.png \
    darkstyle/icon_checkbox_checked.png \
    darkstyle/icon_checkbox_checked_disabled.png \
    darkstyle/icon_checkbox_checked_pressed.png \
    darkstyle/icon_checkbox_indeterminate.png \
    darkstyle/icon_checkbox_indeterminate_disabled.png \
    darkstyle/icon_checkbox_indeterminate_pressed.png \
    darkstyle/icon_checkbox_unchecked.png \
    darkstyle/icon_checkbox_unchecked_disabled.png \
    darkstyle/icon_checkbox_unchecked_pressed.png \
    darkstyle/icon_close.png \
    darkstyle/icon_radiobutton_checked.png \
    darkstyle/icon_radiobutton_checked_disabled.png \
    darkstyle/icon_radiobutton_checked_pressed.png \
    darkstyle/icon_radiobutton_unchecked.png \
    darkstyle/icon_radiobutton_unchecked_disabled.png \
    darkstyle/icon_radiobutton_unchecked_pressed.png \
    darkstyle/icon_undock.png \
    darkstyle/icon_vline.png \
    darkstyle/darkstyle.qss

win32: RC_ICONS = MLVAPP.ico
macx: ICON = MLVAPP.icns
QMAKE_INFO_PLIST = Info.plist
PACKAGE_FILES.files = FFmpeg/ffmpeg #Unzip the file before building the App!!!
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_1808x727.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_1808x1190.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_1872x1060.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_2592x1108.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x726.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x727.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x1190.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1872x1060.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_2592x1108.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1808x727.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1808x1190.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1059.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1060.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_2592x1108.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_1808x727.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_1808x1190.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_1872x1060.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_2592x1108.fpm
PACKAGE_FILES.path = Contents/MacOS
QMAKE_BUNDLE_DATA += PACKAGE_FILES
