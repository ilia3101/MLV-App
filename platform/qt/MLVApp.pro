#-------------------------------------------------
#
# Project created by QtCreator 2017-07-12T15:09:32
#
#-------------------------------------------------

QT       += core gui multimedia
CONFIG   += c++14

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
macx: QMAKE_CXXFLAGS += -ObjC++
macx: LIBS += -framework CoreVideo \
              -framework AVFoundation \
              -framework Cocoa \
              -framework Foundation \
              -framework CoreFoundation \
              -framework CoreMedia

#OpenMP on macOS: first install llvm via brew, setup llvm kit & compiler in Qt settings!
#macx{
#    QMAKE_CC = /usr/local/opt/llvm/bin/clang
#    QMAKE_CXX = /usr/local/opt/llvm/bin/clang++
#    QMAKE_LINK = /usr/local/opt/llvm/bin/clang++
#    QMAKE_CFLAGS += -fopenmp
#    QMAKE_CXXFLAGS += -fopenmp
#    INCLUDEPATH += -I/usr/local/opt/llvm/include
#    LIBS += -L/usr/local/opt/llvm/lib -lgomp
#    QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.8
#}

# Windows
win32: QMAKE_CFLAGS += -O2 -fopenmp -msse4.1 -mssse3 -msse3 -msse2 -msse -D_FILE_OFFSET_BITS=64 -std=c99
win32: QMAKE_CXXFLAGS += -fopenmp
win32: LIBS += -llibgomp-1

# Linux
linux-g++*: QMAKE_CFLAGS += -O3 -fopenmp -msse4.1 -mssse3 -msse3 -msse2 -msse -std=c99
linux-g++*: QMAKE_CXXFLAGS += -fopenmp
linux-g++*: LIBS += -lgomp

##############
SOURCES += \
        main.cpp \
        MainWindow.cpp \
    ../../src/debayer/amaze_demosaic.c \
    ../../src/debayer/debayer.c \
    ../../src/debayer/conv.c \
    ../../src/debayer/dmzhangwu.c \
    ../../src/debayer/basic.c \
    ../../src/ca_correct/CA_correct_RT.c \
    ../../src/matrix/matrix.c \
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
    ../../src/processing/cube_lut.c \
    ../../src/debayer/igv_demosaic.c \
    ../../src/debayer/ahd.c \
    ../../src/mlv/llrawproc/dualiso.c \
    ../../src/dng/dng.c \
    ScopesLabel.cpp \
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
    Scripting.cpp \
    FcpxmlAssistantDialog.cpp \
    FcpxmlSelectDialog.cpp \
    ../../src/processing/denoiser/denoiser_2d_median.c \
    ReceiptCopyMaskDialog.cpp \
    UserManualDialog.cpp \
    QRecentFilesMenu.cpp \
    SingleFrameExportDialog.cpp \
    Curves.cpp \
    ../../src/processing/interpolation/spline_helper.cpp \
    ../../src/processing/interpolation/cosine_interpolation.c \
    ../../src/processing/rbfilter/rbf_wrapper.cpp \
    HueVsDiagram.cpp \
    ../../src/processing/rbfilter/RBFilterPlain.cpp \
    ../../src/debayer/wb_conversion.c \
    ../../src/processing/sobel/sobel.c \
    MoveToTrash.cpp \
    OverwriteListDialog.cpp \
    PixelMapListDialog.cpp \
    ../../src/processing/cafilter/ColorAberrationCorrection.c \
    NoScrollSpinBox.cpp \
    NoScrollComboBox.cpp \
    TranscodeDialog.cpp

macx: SOURCES += ../cocoa/avf_lib/avf_lib.m

HEADERS += \
        MainWindow.h \
    ../../src/debayer/debayer.h \
    ../../src/debayer/helpersse2.h \
    ../../src/debayer/conv.h \
    ../../src/debayer/dmzhangwu.h \
    ../../src/debayer/basic.h \
    ../../src/ca_correct/CA_correct_RT.h \
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
    ../../src/processing/cube_lut.h \
    ../../src/processing/denoiser/denoiser_2d_median.h \
    ../../src/processing/bmd_film.h \
    InfoDialog.h \
    MyApplication.h \
    ScopesLabel.h \
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
    Scripting.h \
    FcpxmlAssistantDialog.h \
    FcpxmlSelectDialog.h \
    ReceiptCopyMaskDialog.h \
    UserManualDialog.h \
    QRecentFilesMenu.h \
    SingleFrameExportDialog.h \
    StretchFactors.h \
    DarkStyleModern.h \
    Curves.h \
    ../../src/processing/interpolation/spline_helper.h \
    ../../src/processing/interpolation/spline.h \
    ../../src/processing/interpolation/cosine_interpolation.h \
    HueVsDiagram.h \
    ../../src/processing/rbfilter/rbf_wrapper.h \
    ../../src/processing/rbfilter/rbf.h \
    ../../src/processing/rbfilter/RBFilterPlain.h \
    FpmInstaller.h \
    ../../src/processing/sobel/sobel.h \
    avir/avir.h \
    MoveToTrash.h \
    OverwriteListDialog.h \
    avir/avirthreadpool.h \
    avir/ThreadPool.h \
    PixelMapListDialog.h \
    ../../src/processing/cafilter/ColorAberrationCorrection.h \
    NoScrollSpinBox.h \
    NoScrollComboBox.h \
    TranscodeDialog.h

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
    Updater/updaterUI/cupdaterdialog.ui \
    FcpxmlAssistantDialog.ui \
    FcpxmlSelectDialog.ui \
    ReceiptCopyMaskDialog.ui \
    UserManualDialog.ui \
    SingleFrameExportDialog.ui \
    OverwriteListDialog.ui \
    PixelMapListDialog.ui \
    TranscodeDialog.ui

RESOURCES += \
    ressources.qrc \
    darkstyle.qrc

DISTFILES += \
    Info.plist \
    MLVAPP.ico \
    darkstyle/darkstyle.qss \
    darkstyle/darkstyleOSX.qss \
    mlvapp.desktop

RC_ICONS = MLVAPP.ico
#win32{
#    RC_ICONS = MLVAPP.ico
#    QMAKE_TARGET_COMPANY = magiclantern
#    QMAKE_TARGET_DESCRIPTION = "Processing and converting tool for MLV files"
#    QMAKE_TARGET_PRODUCT = MLVApp
#    VERSION = 0.17.0
#    RC_CODEPAGE = 1252
#}
macx: ICON = MLVAPP.icns
QMAKE_INFO_PLIST = Info.plist
PACKAGE_FILES.files += bash_scripts/HDR_MOV.command
PACKAGE_FILES.files += bash_scripts/TIF_CLEAN.command
PACKAGE_FILES.files += bash_scripts/PROXY_CLEANER.command
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1059.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1060.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1951.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000326_1808x3508.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1872x1190.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1736x2181.fpm
PACKAGE_FILES.files += ../../src/mlv/llrawproc/pixelmaps/80000331_1808x1951.fpm
PACKAGE_FILES.path = Contents/MacOS
QMAKE_BUNDLE_DATA += PACKAGE_FILES
#unpack & install ffmpeg on OSX
macx: QMAKE_POST_LINK += unzip -o ../qt/FFmpeg/ffmpegOSX.zip $$escape_expand(\n\t)
macx: QMAKE_POST_LINK += "mv ffmpeg MLV\ App.app/Contents/MacOS/" $$escape_expand(\n\t)
#unpack & install raw2mlv on OSX
macx: QMAKE_POST_LINK += unzip -o ../qt/raw2mlv/raw2mlvOSX.zip $$escape_expand(\n\t)
macx: QMAKE_POST_LINK += "mv raw2mlv MLV\ App.app/Contents/MacOS/" $$escape_expand(\n\t)

unix{
    OBJECTS_DIR = .obj
    MOC_DIR = .moc
    UI_DIR = .ui
    RCC_DIR = .rcc
}

#for using the compiled version
linux-g++ {
    target.path = $$(HOME)/bin
    desktop.path = $$(HOME)/.local/share/applications
    desktop.files += mlvapp.desktop
    icon512.path = $$(HOME)/.local/share/icons/hicolor/512x512/apps
    icon512.files += RetinaIMG/MLVAPP.png

    INSTALLS += target desktop icon512

    QMAKE_POST_LINK += tar -C $$(HOME)/bin -xvJf $$_PRO_FILE_PWD_/FFmpeg/ffmpegLinux.tar.xz --strip-components=1 --wildcards */ffmpeg $$escape_expand(\n\t)
    QMAKE_POST_LINK += tar -C $$(HOME)/bin -xvJf $$_PRO_FILE_PWD_/raw2mlv/raw2mlvLinux.tar.xz --strip-components=1 --wildcards */raw2mlv $$escape_expand(\n\t)
}

#for using linuxdeployqt
#->run "make INSTALL_ROOT=. -j$(nproc) install" (is possible via QtCreators Project tab, add build step (make))
#->run via terminal "linuxdeployqt-continuous-x86_64.AppImage path/to/appdir/usr/share/applications/mlvapp.desktop -appimage -qmake=pathToQmake/qmake"
#linux-g++ {
#    DEFINES += APP_IMAGE

#    QMAKE_POST_LINK += tar -C ../qt/FFmpeg/ -xvJf ../qt/FFmpeg/ffmpegLinux.tar.xz --strip=1 --wildcards */ffmpeg $$escape_expand(\n\t)
#    QMAKE_POST_LINK += chmod a+x ../qt/FFmpeg/ffmpeg $$escape_expand(\n\t)

#    QMAKE_POST_LINK += tar -C ../qt/raw2mlv/ -xvJf ../qt/raw2mlv/raw2mlvLinux.tar.xz --strip=1 --wildcards */raw2mlv $$escape_expand(\n\t)
#    QMAKE_POST_LINK += chmod a+x ../qt/raw2mlv/raw2mlv $$escape_expand(\n\t)

#    isEmpty(PREFIX) {
#        PREFIX = /usr
#    }
#    target.path = $$PREFIX/bin
#    ffmpegSt.path = $$PREFIX/bin
#    ffmpegSt.files += FFmpeg/ffmpeg
#    desktop.path = $$PREFIX/share/applications
#    desktop.files += mlvapp.desktop
#    icon512.path = $$PREFIX/share/icons/hicolor/512x512/apps
#    icon512.files += RetinaIMG/MLVAPP.png
#    INSTALLS += ffmpegSt
#    INSTALLS += icon512
#    INSTALLS += desktop
#    INSTALLS += target
#}
