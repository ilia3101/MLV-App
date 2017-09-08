#!/bin/sh

# Makes the MLV App icon from scratch, you must have Blender for this to work

# Remove old stuff
rm icon.icns; rm icon.png;

# Render the icon. THIS WILL TAKE MINUTES.
/Applications/Blender.app/Contents/MacOS/blender -b icon.blend -o icon# -f 1;
# NOTE: I have left the blend file set on CPU render, but switch to GPU if you have an NVIDIA card

# QyK REnAM3
mv icon1.png icon.png;

# Run png2icns
./png2icns.sh icon.png icon.icns;