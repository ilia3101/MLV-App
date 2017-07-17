# Name of app in variable for easy changing
appname = "MLV App"

# List of all objects to link
objects = main.o video_mlv.o debayer.o amaze_demosaic.o raw_processing.o \
	      main_methods.o useful_methods.o background_thread.o matrix.o \
		  camera_matrices.o frame_caching.o bitmap.o

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
	make build; \
	rm compile_time_code; \
	rm app_window_title.h

# Actual linking and compiling happens here
build : $(objects)
	$(CC) $(mainflags) $(objects) -o $(appname) -framework Cocoa;

# Making all objects...
main.o : main.m
	$(CC) $(cflags) main.m

main_methods.o : main_methods.m
	$(CC) $(cflags) main_methods.m

background_thread.o : background_thread.m
	$(CC) $(cflags) background_thread.m

useful_methods.o : gui_stuff/useful_methods.m
	$(CC) $(cflags) gui_stuff/useful_methods.m

video_mlv.o : ../../src/mlv/video_mlv.c
	$(CC) $(cflags) ../../src/mlv/video_mlv.c

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

bitmap.o : ../../src/imageio/bitmap/bitmap.c
	$(CC) $(cflags) ../../src/imageio/bitmap/bitmap.c


# Type 'make clean' to remove mess
.PHONY : clean
clean : # Removes the program and object files 
	rm $(appname) $(objects)
