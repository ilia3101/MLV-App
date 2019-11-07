SourceFiles=(
    ../../src/debayer/amaze_demosaic.c
    ../../src/debayer/debayer.c
    ../../src/debayer/conv.c
    ../../src/debayer/dmzhangwu.c
    ../../src/debayer/basic.c
    ../../src/ca_correct/CA_correct_RT.c
    ../../src/matrix/matrix.c
    ../../src/mlv/frame_caching.c
    ../../src/mlv/video_mlv.c
    ../../src/mlv/liblj92/lj92.c
    ../../src/mlv/llrawproc/llrawproc.c
    ../../src/mlv/llrawproc/pixelproc.c
    ../../src/mlv/llrawproc/stripes.c
    ../../src/mlv/llrawproc/patternnoise.c
    ../../src/mlv/llrawproc/hist.c
    ../../src/mlv/camid/camera_id.c
    ../../src/processing/raw_processing.c
    ../../src/processing/filter/filter.c
    ../../src/processing/sobel/sobel.c
    ../../src/processing/filter/genann/genann.c
    ../../src/processing/cube_lut.c
    ../../src/debayer/igv_demosaic.c
    ../../src/debayer/ahd.c
    ../../src/mlv/llrawproc/dualiso.c
    ../../src/dng/dng.c
    ../../src/mlv/llrawproc/darkframe.c
    ../../src/mlv/audio_mlv.c
    ../../src/processing/blur_threaded.c
    ../../src/processing/denoiser/denoiser_2d_median.c
    ../../src/processing/interpolation/cosine_interpolation.c
    ../../src/debayer/wb_conversion.c
    ../../src/ca_correct/CA_correct_RT.c
    ../../src/processing/cafilter/ColorAberrationCorrection.c
);

SourceFilesCPP=(
    ../../src/processing/interpolation/spline_helper.cpp
    ../../src/processing/rbfilter/rbf_wrapper.cpp
    ../../src/processing/rbfilter/RBFilterPlain.cpp
);

for i in "${SourceFiles[@]}"
do
   gcc -c -O3 $i
done

for i in "${SourceFilesCPP[@]}"
do
   g++ -c -O3 $i
done

g++ -c -O3 -std=c++17 main.cpp `pkg-config gtkmm-3.0 --cflags --libs`
g++ -c -O3 -std=c++17 MLVBlenderParameterView.cpp `pkg-config gtkmm-3.0 --cflags --libs`
gcc -c -O3 MLVBlender.c

g++ *.o `pkg-config gtkmm-3.0 --cflags --libs` -lm -lpthread -lgomp -lepoxy -lGL -o MLVStitch

# rm *.o
