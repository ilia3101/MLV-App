Steps to pack the image:
- <path to qt>/bin/macdeployqt <path to MLVApp release>/MLV\ App.app
- sudo codesign --force --deep --sign - <path to MLVApp release>/MLV\ App.app/Contents/MacOS/MLV\ App
- copy "MLV App.app" to app folder
- ./BuildInstaller.sh