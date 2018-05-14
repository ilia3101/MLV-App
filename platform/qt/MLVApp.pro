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
# OSX
macx: QMAKE_CXXFLAGS_DEBUG += -ObjC++
macx: QMAKE_CXXFLAGS_RELEASE += -ObjC++
macx: LIBS += -framework CoreVideo \
              -framework AVFoundation \
              -framework Cocoa \
              -framework Foundation \
              -framework CoreFoundation \
              -framework CoreMedia


# Windows
win32: QMAKE_CFLAGS_DEBUG += -O2 -msse4.1 -mssse3 -msse3 -msse2 -msse -D_FILE_OFFSET_BITS=64 -std=c99
win32: QMAKE_CFLAGS_RELEASE += -O2 -msse4.1 -mssse3 -msse3 -msse2 -msse -D_FILE_OFFSET_BITS=64 -std=c99

# Linux
linux-g++*: QMAKE_CFLAGS_DEBUG += -O3 -msse4.1 -mssse3 -msse3 -msse2 -msse -std=c99
linux-g++*: QMAKE_CFLAGS_RELEASE += -O3 -msse4.1 -mssse3 -msse3 -msse2 -msse -std=c99

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
    ../../src/mlv/camid/camera_id.c \
    ../../src/processing/processing.c \
    ../../src/processing/raw_processing.c \
    ../../src/processing/filter/filter.c \
    ../../src/processing/filter/genann/genann.c \
    ../../src/processing/image_profiles.c \
    ../../src/mlv/llrawproc/dualiso.c \
    ../../src/dng/dng.c \
    InfoDialog.cpp \
    StatusDialog.cpp \
    Histogram.cpp \
    WaveFormMonitor.cpp \
    ExportSettingsDialog.cpp \
    ReceiptSettings.cpp \
    EditSliderValueDialog.cpp \
    DoubleClickLabel.cpp \
    AudioWave.cpp \
    ResizeLabel.cpp \
    GraphicsZoomView.cpp \
    JumpSlider.cpp \
    AudioPlayback.cpp \
    GraphicsPickerScene.cpp \
    NoScrollSlider.cpp \
    ColorToolButton.cpp \
    RenderFrameThread.cpp \
    GraphicsPolygonMoveItem.cpp \
    GradientElement.cpp \
    VectorScope.cpp \
    TimeCodeLabel.cpp \
    ../../src/mlv/llrawproc/darkframe.c \
    ColorWheel.cpp \
    ../../src/mlv/audio_mlv.c \
    Updater/updaterUI/cupdaterdialog.cpp \
    Updater/cautoupdatergithub.cpp \
    Updater/updaterUI/CUpdater.cpp \
    ../../src/processing/blur_threaded.c \
    Scripting.cpp

macx: SOURCES += ../cocoa/avf_lib/avf_lib.m

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
    ../../src/mlv_include.h \
    ../../src/mlv/llrawproc/dualiso.h \
    ../../src/mlv/camid/camera_id.h \
    ../../src/dng/dng.h \
    ../../src/dng/dng_tag_codes.h \
    ../../src/dng/dng_tag_types.h \
    ../../src/dng/dng_tag_values.h \
    ../../src/processing/processing_object.h \
    ../../src/processing/raw_processing.h \
    ../../src/processing/filter/filter.h \
    ../../src/processing/filter/film.h \
    ../../src/processing/filter/genann/genann.h \
    ../../src/processing/image_profile.h \
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
    DarkStyle.h \
    JumpSlider.h \
    AudioPlayback.h \
    GraphicsPickerScene.h \
    NoScrollSlider.h \
    ColorToolButton.h \
    RenderFrameThread.h \
    GraphicsPolygonMoveItem.h \
    GradientElement.h \
    VectorScope.h \
    TimeCodeLabel.h \
    ../../src/mlv/llrawproc/darkframe.h \
    ColorWheel.h \
    ../../src/mlv/audio_mlv.h \
    ../../src/mlv/macros.h \
    Updater/updaterUI/cupdaterdialog.h \
    Updater/cautoupdatergithub.h \
    Updater/updaterUI/CUpdater.h \
    ../../src/processing/blur_threaded.h \
    Scripting.h

macx: HEADERS += \
    ../cocoa/avf_lib/avencoder.h \
    ../cocoa/avf_lib/avf_lib.h \
    AvfLibWrapper.h

FORMS += \
        MainWindow.ui \
    InfoDialog.ui \
    StatusDialog.ui \
    ExportSettingsDialog.ui \
    EditSliderValueDialog.ui \
    Updater/updaterUI/cupdaterdialog.ui

RESOURCES += \
    ressources.qrc \
    darkstyle.qrc

DISTFILES += \
    Info.plist \
    MLVAPP.ico \
    darkstyle/darkstyle.qss \
    darkstyle/darkstyleOSX.qss \
    mlvapp.desktop

win32: RC_ICONS = MLVAPP.ico
macx: ICON = MLVAPP.icns
QMAKE_INFO_PLIST = Info.plist
PACKAGE_FILES.files = FFmpeg/ffmpeg #Unzip the file before building the App!!!
PACKAGE_FILES.files += bash_scripts/HDR_MOV.command
PACKAGE_FILES.files += bash_scripts/tif_clean.command
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_1808x727.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_1808x1190.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_1872x1060.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000301_2592x1108.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x726.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x727.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x1190.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1872x1060.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_2592x1108.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1808x727.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1808x1190.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1059.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1060.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_2592x1108.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_1808x727.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_1808x1190.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_1872x1060.fpm
#PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000346_2592x1108.fpm
PACKAGE_FILES.path = Contents/MacOS
QMAKE_BUNDLE_DATA += PACKAGE_FILES

#linux-g++ {
    #Add desktop file
#    EXTRA_FILES += \
#        mlvapp.desktop
#    for(FILE,EXTRA_FILES){
#        QMAKE_POST_LINK += $$quote(cp $${FILE} $${DESTDIR/usr/share/applications/}$$escape_expand(\n\t))
#    }
#}

