# Name of app in variable for easy changing
appname = "MLV App"

# List of all objects to link
objects = main.o video_mlv.o debayer.o amaze_demosaic.o raw_processing.o \
	      main_methods.o useful_methods.o background_thread.o matrix.o \
		  camera_matrices.o frame_caching.o lj92.o session_methods.o \
		  delegate.o mlv_view.o llrawproc.o pixelproc.o stripes.o \
		  patternnoise.o hist.o dualiso.o avf_lib.o filter.o genann.o \
		  blur_threaded.o dng.o darkframe.o camera_id.o audio_mlv.o

# All macOS frameworks for the link
frameworks = -framework Cocoa -framework AppKit -framework Foundation \
	   		 -framework CoreFoundation -framework OpenGL -framework CoreMedia \
			 -framework AVFoundation -framework CoreVideo

# Compilers
CC = clang # C
OCC = clang # Objective C

# Flags for link and objects
mainflags = -mmacosx-version-min=10.10 -O3 -Ofast -m64 -DNDEBUG
linkflags = $(mainflags) -lm -pthread -lobjc
cflags := $(mainflags) -c -std=c99
objcflags := $(cflags) -x objective-c

# Run some cheeky compiletime code to generate window title with hostname
# Then call the build rule to actually build it
main :
	$(OCC) $(mainflags) compile_time_code.m -o compile_time_code -framework Cocoa; \
	./compile_time_code; \
	rm main.o; \
	make build; \
	rm compile_time_code; \
	rm app_defines.h

# run 'make app' to get a .app bundle
# we steal masc's Qt ffmpeg :) (remember, ffmpeg is temporary in Cocoa app)
app :
	rm $(appname).app; \
	$(CC) $(mainflags) generate_info_plist.c -o generate_info_plist; \
	./generate_info_plist $(appname); \
	make main; \
	rm -rf $(appname).app; \
	mkdir $(appname).app; \
	mkdir $(appname).app/Contents; \
	mkdir $(appname).app/Contents/MacOS; \
	mkdir $(appname).app/Contents/Resources; \
	cp -i $(appname) $(appname).app/Contents/MacOS/; \
	cp -i info.plist $(appname).app/Contents/; \
	mkdir $(appname).app/Contents/Resources/pixelmaps; \
	cp ../../src/icon/icon.icns $(appname).app/Contents/Resources/MLV\ App.icns; \
	rm generate_info_plist; \
	rm info.plist;

# Actual linking and compiling happens here
build : $(objects)
	$(CC) $(mainflags) $(objects) -o $(appname) $(frameworks);

# Cocoa app part (Objective C)
main.o : main.m
	$(OCC) $(objcflags) main.m
mlv_view.o : mlv_view.m
	$(OCC) $(objcflags) mlv_view.m
avf_lib.o : avf_lib/avf_lib.m
	$(OCC) $(objcflags) avf_lib/avf_lib.m
main_methods.o : main_methods.m
	$(OCC) $(objcflags) main_methods.m
delegate.o : delegate.m
	$(OCC) $(objcflags) delegate.m
session_methods.o : session_methods.m
	$(OCC) $(objcflags) session_methods.m
background_thread.o : background_thread.m
	$(OCC) $(objcflags) background_thread.m
useful_methods.o : gui_stuff/useful_methods.m
	$(OCC) $(objcflags) gui_stuff/useful_methods.m
preferences_window : preferences_window.m
	$(OCC) $(objcflags) preferences_window.m

# MLV and processing
video_mlv.o : ../../src/mlv/video_mlv.c
	$(CC) $(cflags) ../../src/mlv/video_mlv.c
audio_mlv.o : ../../src/mlv/audio_mlv.c
	$(CC) $(cflags) ../../src/mlv/audio_mlv.c
dng.o : ../../src/dng/dng.c
	$(CC) $(cflags) ../../src/dng/dng.c
lj92.o : ../../src/mlv/liblj92/lj92.c
	$(CC) $(cflags) ../../src/mlv/liblj92/lj92.c
frame_caching.o : ../../src/mlv/frame_caching.c
	$(CC) $(cflags) ../../src/mlv/frame_caching.c
camera_matrices.o : ../../src/mlv/camera_matrices.c
	$(CC) $(cflags) ../../src/mlv/camera_matrices.c
debayer.o : ../../src/debayer/debayer.c
	$(CC) $(cflags) -pthread ../../src/debayer/debayer.c
amaze_demosaic.o : ../../src/debayer/amaze_demosaic.c
	$(CC) $(cflags) ../../src/debayer/amaze_demosaic.c
raw_processing.o : ../../src/processing/raw_processing.c
	$(CC) $(cflags) ../../src/processing/raw_processing.c
filter.o : ../../src/processing/filter/filter.c
	$(CC) $(cflags) ../../src/processing/filter/filter.c
genann.o : ../../src/processing/filter/genann/genann.c
	$(CC) $(cflags) ../../src/processing/filter/genann/genann.c
blur_threaded.o : ../../src/processing/blur_threaded.c
	$(CC) $(cflags) ../../src/processing/blur_threaded.c
matrix.o : ../../src/matrix/matrix.c
	$(CC) $(cflags) ../../src/matrix/matrix.c
camera_id.o : ../../src/mlv/camid/camera_id.c
	$(CC) $(cflags) ../../src/mlv/camid/camera_id.c
llrawproc.o : ../../src/mlv/llrawproc/llrawproc.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/llrawproc.c
darkframe.o : ../../src/mlv/llrawproc/darkframe.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/darkframe.c
pixelproc.o : ../../src/mlv/llrawproc/pixelproc.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/pixelproc.c
stripes.o : ../../src/mlv/llrawproc/stripes.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/stripes.c
patternnoise.o : ../../src/mlv/llrawproc/patternnoise.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/patternnoise.c
hist.o : ../../src/mlv/llrawproc/hist.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/hist.c
dualiso.o : ../../src/mlv/llrawproc/dualiso.c
	$(CC) $(cflags) ../../src/mlv/llrawproc/dualiso.c


# Type 'make clean' to remove mess
.PHONY : clean
clean : # Removes the program and object files 
	rm -rf $(appname) $(appname).app $(objects) *.o
