#!/bin/sh

# Make a macOS .icns file by running $ ./make_icns.sh icon.png icon.icns
# You may need to do $ chmod u+x make_icns.sh for it to work
# This script is Mac only :(

# Make iconset
mkdir icon.iconset;

# Make all sizes. YES, I can't figure out how to do a loop.
cp $1 icon.iconset/icon_512x512@2x.png; sips -Z 1024 icon.iconset/icon_512x512@2x.png;
cp $1 icon.iconset/icon_512x512.png; sips -Z 512 icon.iconset/icon_512x512.png; cp icon.iconset/icon_512x512.png icon.iconset/icon_256x256@2x.png;
cp $1 icon.iconset/icon_256x256.png; sips -Z 256 icon.iconset/icon_256x256.png; cp icon.iconset/icon_256x256.png icon.iconset/icon_128x128@2x.png;
cp $1 icon.iconset/icon_128x128.png; sips -Z 128 icon.iconset/icon_128x128.png;
cp $1 icon.iconset/icon_64x64.png; sips -Z 64 icon.iconset/icon_64x64.png; cp icon.iconset/icon_64x64.png icon.iconset/icon_32x32@2x.png;
cp $1 icon.iconset/icon_32x32.png; sips -Z 32 icon.iconset/icon_32x32.png; cp icon.iconset/icon_32x32.png icon.iconset/icon_16x16@2x.png;
cp $1 icon.iconset/icon_16x16.png; sips -Z 16 icon.iconset/icon_16x16.png;

# Generate .icns file
iconutil -c icns icon.iconset -o $2;

# Remove iconset
rm -rf icon.iconset;