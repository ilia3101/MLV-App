# :fire::fire::fire: MLV App :fire::fire::fire:
What is MLV App? Lightroom, but for Magic Lantern MLV Video (and open source and cross platform)

![MLV App](https://user-images.githubusercontent.com/30245296/64981183-c8bfae80-d8bb-11e9-9b0c-cd1f8041953f.png)

Find the latest official releases [here](https://ilia3101.github.io/MLV-App/). Download, unpack and start. Find debian packages [here](http://phd-sid.ethz.ch/debian/mlv-app/) (thanks to @alexmyczko).

A user manual can be found [here](https://github.com/ilia3101/MLV-App/wiki) and in-app. A nice tutorial video with subtitles can be found [here](https://www.youtube.com/watch?v=X17jzHjuHOo) in russian and [here](https://www.youtube.com/watch?v=-mmnG5uBJok) in english (thanks to Maksim Danilov).

## Features :collision:
- Import MLV files
- Support for spanned mlv (.m00, .m01, ...)
- Support for lossless mlv
- Support for any bit depth (…, 10, 12, 14bit)
- Demosaic algorithms: None, Simple (fastest), bilinear, multithreaded AMaZE & LMMSE, AHD or IGV
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
- Clip export via ffmpeg 10bit ProRes 422 (Proxy, LT, Standard, HQ), ProRes 4444, RAW AVI, 8bit H.264 and H.265, 16bit TIFF sequence, DNxHD, DNxHR, JPEG2000, MotionJPEG, 10/12/16bit HuffYUV 4:4:4; all with or without audio
- macOS only: Clip export via Apple AVFoundation 12bit ProRes 422, ProRes 4444 and 8bit H.264, all with or without audio
- Clip export to (10/12/14/16bit for Dual ISO) Cinema DNG files (with default or Davinci Resolve naming). Exporting modes: uncompressed, lossless and fast pass (in the last one no RAW correction, processing or decompressing/compressing is done, raw data copied as is from MLV to DNG uncompressed or lossless).
- MLV export (fast pass, compressed, averaged frame (for darkframe creation), extract internal darkframe)
- Audio only export
- Clip trimming support (Cut In and Cut Out) for any export mode (ffmpeg, AVFoundation, cDNG, MLV) including audio syncing with correct timecode.
- Frame rate override for export and playback
- Aspect Ratio: stretch width (1.33x, 1.5x, 1.75x, 1.8x, 2.0x) and height (0.3x, 1.67x, 3.0x, 5.0x auto detected for latest MLVs) for playback and export per clip in session. Manually changed AR is inserted to exported cDNG header.
- Upside down transformation
- Resize frame resolution for (batch) export
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
#### Qt App macOS
- install XCode depending on your OSX
- install Qt5 (minimum 5.6)
- open `platform/qt/MLVApp.pro` in QtCreator
- Build and Start
- OR download and doubleclick easy-to-use [compiler app](https://bitbucket.org/Dannephoto/mlv_app_compiler/downloads/mlv_app_compiler.dmg) from @dannephoto

#### Qt App Windows
- install Qt5 (minimum 5.6) with MinGW compiler
- unpack ffmpegWin.zip in `platform/qt/FFmpeg` (and copy it later into build directory)
- open `platform/qt/MLVApp.pro` in QtCreator
- Build and Start

#### Qt App Linux (generally)
- install Qt5 (minimum 5.6) and ffmpeg (we use v3.3.2) 
- `cd platform/qt/`
- `qmake MLVApp.pro` or equivalent (depending on distro and version and...)
- `make -j4` (for quad core)
- `./mlvapp` and have fun

A detailed guide for compiling MLV-App on Linux can be found [here](https://sternenkarten.com/tutorial-englisch/) (thanks to @seescho).

#### Cocoa App (very very deprecated)
```
git clone https://github.com/ilia3101/MLV-App.git
cd MLV-App/platform/cocoa
make app -j4
```

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
