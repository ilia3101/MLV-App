# Name of app in variable for easy changing
appname = "MLV App"

# List of all objects to link
objects = main.o video_mlv.o debayer.o amaze_demosaic.o raw_processing.o \
	      main_methods.o useful_methods.o background_thread.o matrix.o \
		  camera_matrices.o frame_caching.o lj92.o session_methods.o

# Compiler
CC = gcc # Normal

# Flags for link and objects
mainflags = -O3 -Ofast -m64
linkflags = $(mainflags) -mmacosx-version-min=10.6 -lm -pthread
cflags := $(mainflags) -c -mmacosx-version-min=10.6

# Run some cheeky compiletime code to generate window title with hostname
# Then call the build rule to actually build it
main :
	$(CC) compile_time_code.c -o compile_time_code; \
	./compile_time_code; \
	rm main.o; \
	make build; \
	rm compile_time_code; \
	rm app_window_title.h

# run 'make app' to get a .app bundle
# we steal masc's Qt ffmpeg :) (remember, ffmpeg is temporary in Cocoa app)
app :
	rm $(appname).app; \
	$(CC) generate_info_plist.c -o generate_info_plist; \
	./generate_info_plist $(appname); \
	make main; \
	rm -rf $(appname).app; \
	mkdir $(appname).app; \
	mkdir $(appname).app/Contents; \
	mkdir $(appname).app/Contents/MacOS; \
	mkdir $(appname).app/Contents/Resources; \
	cp -i $(appname) $(appname).app/Contents/MacOS/; \
	cp -i info.plist $(appname).app/Contents/; \
	unzip ../qt/FFmpeg/ffmpegOSX.zip -d $(appname).app/Contents/Resources; \
	rm -rf $(appname).app/Contents/Resources/__MACOSX; \
	rm generate_info_plist; \
	rm info.plist; \

# Actual linking and compiling happens here
build : $(objects)
	$(CC) $(mainflags) $(objects) -o $(appname) -framework Cocoa;

# Making all objects...
main.o : main.m
	$(CC) $(cflags) main.m

main_methods.o : main_methods.m
	$(CC) $(cflags) main_methods.m

session_methods.o : session_methods.m
	$(CC) $(cflags) session_methods.m

background_thread.o : background_thread.m
	$(CC) $(cflags) background_thread.m

useful_methods.o : gui_stuff/useful_methods.m
	$(CC) $(cflags) gui_stuff/useful_methods.m

video_mlv.o : ../../src/mlv/video_mlv.c
	$(CC) $(cflags) ../../src/mlv/video_mlv.c

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

matrix.o : ../../src/matrix/matrix.c
	$(CC) $(cflags) ../../src/matrix/matrix.c


# Type 'make clean' to remove mess
.PHONY : clean
clean : # Removes the program and object files 
	rm -rf $(appname) $(appname).app $(objects)
