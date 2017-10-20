# :fire::fire::fire: MLV App :fire::fire::fire:
What is MLV App? A cross platform RAW conversion software that works **natively** with Magic Lantern MLV files.

![MLVApp Qt](https://image.ibb.co/h5tJ0m/Bildschirmfoto.png)

## Compiling :collision:
#### Cocoa App
```
git clone https://github.com/ilia3101/MLV-App.git
cd MLV-App/platform/cocoa
make app -j4
```

#### Qt App macOS
- install XCode depending on your OSX
- install Qt5 (minimum 5.6)
- unpack libpng-1.6.31.tar.gz (don't know how to do in terminal)
- `cd platform/qt/libpng16/` `./configure`    `make check`     `sudo make install`
- unpack ffmpegOSX.zip in `platform/qt/FFmpeg`
- open `platform/qt/MLVApp.pro` in QtCreator
- go to tab project, add command line argument -j4 (for quad core) under build steps, uncheck Add build library search path to DYLD_LIBRARY_PATH and DYLD_FRAMEWORK_PATH checkbox in my project Run section
- Build and Start

#### Qt App Windows
- install Qt5 (minimum 5.6)
- unpack ffmpegWin.zip in `platform/qt/FFmpeg` (and copy it later into build directory)
- open `platform/qt/MLVApp.pro` in QtCreator
- go to tab project, add command line argument -j4 (for quad core) under build steps, uncheck Add build library search path to DYLD_LIBRARY_PATH and DYLD_FRAMEWORK_PATH checkbox in my project Run section
- Build and Start

#### Qt App Linux (generally)
- install Qt5 (minimum 5.6), ffmpeg (we use v3.3.2) and zlib (the steps to get these three items are different on all distros)
- `cd platform/qt/libpng16/`
- unpack libpng-1.6.31.tar.gz (don't know how to do in terminal)
- `./configure`    `make check`     `sudo make install`
- `cd..`
- `qmake MLVApp.pro` or equivalent (depending on distro and version and...)
- `make -j4` (for quad core)
- `./mlvapp` and have fun

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
