# :fire::fire::fire: MLV App :fire::fire::fire:
What is MLV App? Lightroom, but for Magic Lantern MLV Video (and open source and cross platform)

[![Lates Release](https://img.shields.io/github/v/release/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/releases)

[![Qt&5](https://img.shields.io/badge/Qt-5-brightgreen)](https://doc.qt.io/qt-5/)
[![Qt&6](https://img.shields.io/badge/Qt-6-brightgreen)](https://doc.qt.io/qt-6/)

[![Commit Activity](https://img.shields.io/github/commit-activity/m/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/pulse)
[![Last Commit](https://img.shields.io/github/last-commit/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/pulse)
[![Contributors](https://img.shields.io/github/contributors/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/graphs/contributors)

[![Open Issues](https://img.shields.io/github/issues/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/issues)
[![Closed Issues](https://img.shields.io/github/issues-closed/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/issues?q=is%3Aissue+is%3Aclosed)

[![Open PRs](https://img.shields.io/github/issues-pr/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/pulls)
[![Closed PRs](https://img.shields.io/github/issues-pr-closed/ilia3101/MLV-App)](https://github.com/ilia3101/MLV-App/pulls?q=is%3Apr+is%3Aclosed)

![MLV App](https://user-images.githubusercontent.com/30245296/168381368-31cf5666-ea2d-4efa-b21f-2f3a5ac456ce.png)

## Useful links
- [Find the latest official releases here](https://mlv.app). Download, unpack and start
- Find debian packages [here](http://sid.ethz.ch/debian/mlv-app/) (thanks to @alexmyczko)
- Find Arch Linux image [here](https://aur.archlinux.org/packages/mlv.app/) (thanks to davvore33)
- Find NixOS package [here](https://search.nixos.org/packages?show=mlv-app)
- Ask questions on the [Magic Lantern forum thread](https://www.magiclantern.fm/forum/index.php?topic=20025.0)
- Report bugs on the [issues page](https://github.com/ilia3101/MLV-App/issues)
- A user manual can be found [here](https://github.com/ilia3101/MLV-App/wiki) and in-app
- A nice tutorial video with subtitles can be found [here](https://www.youtube.com/watch?v=X17jzHjuHOo) in Russian and [here](https://www.youtube.com/watch?v=-mmnG5uBJok) in English (thanks to Maksim Danilov)

## Features :collision:
- Import MLV files
- Support for spanned mlv (.m00, .m01, ...)
- Support for lossless mlv
- Support for any bit depth (…, 10, 12, 14bit)
- Demosaic algorithms: None, Simple (fastest), AHD, multithreaded bilinear and AMaZE; and highly optimized LMMSE, DCB, RCD and IGV offered by [librtprocess](https://github.com/CarVac/librtprocess)
- Processing with many parameters (exposure, contrast, white balance, clarity, vibrance, saturation, dark & light adjustments, highlights & shadows, gradation curves, sharpen, hue vs. hue/saturation/luminance curves, toning, …)
- Processing filters for film emulation powered by neural networks :ghost:
- Processing in sRGB or several LOG
- RAW corrections (fix focus & bad pixels, chroma smoothing, pattern noise, vertical stripes, adjust RAW black & white level)
- Autodetection for focus pixels and vertical stretching
- MLV darkframe subtraction (with external / internal darkframe)
- Support for dual ISO
- Support for HDR (blending on ffmpeg export)
- Whitebalance picker
- Show clip information
- Analysis: histogram (including markers for under-/overexposed), waveform monitor, RGB parade, vector scope
- Session: open, import to, delete from, save, copy receipt, paste receipt (also batch paste), reset receipt, receipt file import & export, preview pictures for all clips
- Video playback in 2 modes: show each frame or drop frame mode (a kind of realtime playback which shows as many frames your computer can render)
- Audio playback in video drop frame mode
- Loop playback
- Show next frame, previous frame and scroll though timeline
- Label for timecode and duration of edited clip
- Auto load white balance, if MLV was filmed at sunny, shade, cloudy, thungsten, fluorescent, flash or kelvin
- 1D/3D LUT (.cube) support
- Single frame (3x)8bit PNG and Cinema DNG (compressed/lossless) export
- Clip export via ffmpeg 10bit ProRes 422 (Proxy, LT, Standard, HQ), ProRes 4444, RAW AVI, 8bit H.264 and 8/10/12bit H.265, 16bit TIFF sequence, DNxHD, DNxHR, JPEG2000, MotionJPEG, 10/12/16bit HuffYUV 4:4:4, 10bit 4:2:2 & 12bit 4:4:4 CineForm, VP9; all with or without audio
- macOS only: Clip export via Apple AVFoundation 12bit ProRes 422, ProRes 4444 and 8bit H.264, all with or without audio
- Clip export to (10/12/14/16bit for Dual ISO) Cinema DNG files (with default or Davinci Resolve naming). Exporting modes: uncompressed, lossless and fast pass (in the last one no RAW correction, processing or decompressing/compressing is done, raw data copied as is from MLV to DNG uncompressed or lossless).
- MLV export (fast pass, compressed, averaged frame (for darkframe creation), extract internal darkframe)
- Audio only export
- Clip trimming support (Cut In and Cut Out) for any export mode (ffmpeg, AVFoundation, cDNG, MLV) including audio syncing with correct timecode.
- Frame rate override for export and playback
- Aspect Ratio: stretch width (1.33x, 1.5x, 1.75x, 1.8x, 2.0x) and height (0.3x, 1.67x, 3.0x, 5.0x auto detected for latest MLVs) for playback and export per clip in session. Manually changed AR is inserted to exported cDNG header.
- Upside down transformation
- Resize frame resolution for (batch) export using [AVIR](https://github.com/avaneev/avir) resizing algorithm
- Clip batch export
- Smooth artifacts filter (minimizes moiree) for all ffmpeg export codecs, realized by ffmpeg filter combination
- 2D median denoiser (don't expect wonders!)
- Recursive bilateral filter (works as denoiser) with Luminance, Chroma and Range parameter
- Vignette effect / correction with variable shape and chromatic abberation correction
- Post export scripting on macOS (thx @dannephoto)
- Zoom: fit to screen, 100% and free zoom, scroll through picture
- Zebras
- Create and load MAPP files (this files include all required information from the original MLV, plus video and audio frame index. If .MAPP file already created, importing is lot faster especially on slower HDD)
- FCPXML Import Assistant: helps importing the MLV files, which were used as proxy in a NLE project
- FCPXML Selection Assistant: helps selecting the MLV files, which were or were not used as proxy in a NLE project
- Update checker

## Compiling :collision:
#### Qt App macOS (Intel based)
- install XCode depending on your OSX, or llvm via brew for multithreading
- install Qt5 (5.6 .. 5.15.2) or Qt6 (6.4 or later)
- open `platform/qt/MLVApp.pro` in QtCreator
- Build and Start

#### Qt App macOS (Apple Silicon based, with Qt5)
- install command line tools (SDK 11.3 is known to work)
- install brew `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
- install dependencies `brew install pcre2 harfbuzz freetype`
- install compiler `brew install llvm@11`, add entries to PATH as written in terminal output
- install QtCreator `brew install --cask qt-creator`
- clone qt sources `git clone git://code.qt.io/qt/qt5.git` and `cd qt5` and `git checkout 5.15` and `./init-repository`
- build qt `cd ..` and `mkdir qt5-5.15-macOS-release` and `cd qt5-5.15-macOS-release` and `../qt5/configure -release -prefix ./qtbase -nomake examples -nomake tests QMAKE_APPLE_DEVICE_ARCHS=arm64 -opensource -confirm-license' and 'make -j15`
- configure QtCreator build kit with installed llvm@11 and compiled qt
- open `platform/qt/MLVApp.pro` in QtCreator
- uncomment section for Apple Silicon in `MLVApp.pro`
- Build and Start
- OR download and doubleclick easy-to-use [compiler app](https://bitbucket.org/Dannephoto/mlv_app_compiler-git/downloads/mlv_app_compiler_arm64.dmg) from @dannephoto

#### Qt App macOS (Apple Silicon based, with Qt6)
- same as Qt App macOS (Intel based)

#### Qt App Windows
- install Qt5 (Win32: 5.6 .. 5.15.2, Win64: 5.13.2 .. 5.15.2) or Qt6 (6.5 or later) with MinGW32/64 compiler
- unpack ffmpegWin.zip in `platform/qt/FFmpeg` (and copy it later into build directory)
- open `platform/qt/MLVApp.pro` in QtCreator
- Build and Start

#### Qt App Linux (generally)
- install Qt5 (5.6 .. 5.15.2) or Qt6 (6.4 or later), ffmpeg (we use v3.3.2) and other needed packages. We install this in the github Linux runner:
  ```
  sudo apt-get install --no-install-recommends make g++ qt5-qmake qtbase5-dev qtmultimedia5-dev libqt5multimedia5 libqt5multimedia5-plugins libqt5opengl5-dev libqt5designer5 libqt5svg5-dev libfuse2 libxkbcommon-x11-0 appstream
  ```
- `cd platform/qt/`
- `qmake MLVApp.pro` or equivalent (depending on distro and version and...)
- `make -j$(nproc)`
- `./mlvapp` and have fun

A detailed guide for compiling MLV-App on Linux can be found [here](https://sternenkarten.com/tutorial-englisch/) (thanks to @seescho).

#### Cocoa App (very very deprecated)
```
git clone https://github.com/ilia3101/MLV-App.git
cd MLV-App/platform/cocoa
make app -j4
```

## Install MLVApp on mobile systems
#### Android (via Wine)
- install winlator app
- install Win64 MLVApp release version into winlator app

## The Code
All the MLV stuff is in src/mlv

Image processing is in src/processing

Other stuff also in src/...

Platform specific/GUI things in platform/...

#### A note about code style
You may notice a strange mixture of these styles:
1. `thisNameStyle`
2. `this_name_style`

The rule I have used in the libraries is: public functions use the thisNameStyle, and private functions use this_one.
Keep it in mind if you're going to be adding something major.

### Thanks for reading README.md

:frog:
