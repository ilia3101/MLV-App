#clueless at makefiles...

# Name of app
appname = test

# Compiler name
CC = gcc

# Get OS name
UNAME := $(shell uname)

# To compile on Windows use mingw32-fdfw64 thing

# Append '.exe' if windows
ifeq ($(OS), Windows_NT)
    appname := $(appname).exe
endif

# List of all objects to link
objects = main.o bitmap.o debayer.o amaze_demosaic.o  \
	raw_processing.o video_mlv.o matrix.o camera_matrices.o \
	frame_caching.o

# Flags for link and objects
mainflags = -Ofast -DNDEBUG -Wall #-Werror # no debugging

ifeq ($(UNAME), Darwin) # Minimum OSX version if mac
	mainflags := $(mainflags) -mmacosx-version-min=10.5
endif

cflags := $(mainflags) -c -std=c99
# AMaZE seems to fit no standards I try
amazeflags := $(mainflags) -c

# Link all objects with main flags
main : $(objects)
	$(CC) $(mainflags) $(objects) -o $(appname)

# Making all objects...
main.o : main.c
	$(CC) $(cflags) main.c

matrix.o : ../../src/matrix/matrix.c
	$(CC) $(cflags) ../../src/matrix/matrix.c

bitmap.o : ../../src/imageio/bitmap/bitmap.c
	$(CC) $(cflags) ../../src/imageio/bitmap/bitmap.c

debayer.o : ../../src/debayer/debayer.c
	$(CC) $(cflags) -pthread ../../src/debayer/debayer.c

amaze_demosaic.o : ../../src/debayer/amaze_demosaic.c
	$(CC) $(amazeflags) ../../src/debayer/amaze_demosaic.c

raw_processing.o : ../../src/processing/raw_processing.c
	$(CC) $(cflags) ../../src/processing/raw_processing.c

video_mlv.o : ../../src/mlv/video_mlv.c
	$(CC) $(cflags) ../../src/mlv/video_mlv.c

frame_caching.o : ../../src/mlv/frame_caching.c
	$(CC) $(amazeflags) ../../src/mlv/frame_caching.c

camera_matrices.o : ../../src/mlv/camera_matrices.c
	$(CC) $(cflags) ../../src/mlv/camera_matrices.c

# 'make clean' to remove ugly .o files
# !working on Windows(mingw32) for some reason
.PHONY : clean
clean : # Removes the program and object files 
	rm $(appname) $(objects)