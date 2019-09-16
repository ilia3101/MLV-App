#!/bin/sh
#Copy deployed "MLV App.app" to ./App directory before running this
#Copy content from create-dmg (https://github.com/andreyvit/create-dmg) to ./create-dmg-master
test -f "MLVApp.v1.9.OSX.dmg" && rm "MLVApp.v1.9.OSX.dmg"
create-dmg-master/create-dmg \
--volname "MLVApp v1.9 Installer" \
--volicon "app/MLV App.app/Contents/Resources/MLVAPP.icns" \
--background "dmg-background.png" \
--window-pos 200 120 \
--window-size 660 400 \
--icon-size 100 \
--icon "MLV App.app" 165 175 \
--hide-extension "MLV App.app" \
--app-drop-link 495 175 \
"MLVApp.v1.9.OSX.dmg" \
"./app/"
