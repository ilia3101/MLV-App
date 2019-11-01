#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include "blur_threaded.h"

#ifdef __SSE2__
  #include <emmintrin.h>
#endif

#include "raw_processing.h"
#include "../mlv/video_mlv.h"
#include "filter/filter.h"
#include "denoiser/denoiser_2d_median.h"
#include "../mlv/camid/camera_id.h"
#include "interpolation/spline_helper.h"
#include "interpolation/cosine_interpolation.h"
#include "rbfilter/rbf_wrapper.h"
#include "sobel/sobel.h"
#include "cafilter/ColorAberrationCorrection.h"

/* Matrix functions which are useful */
#include "../matrix/matrix.h"

#define STANDARD_GAMMA 3.15

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

#ifndef __APPLE__
#define M_PI 3.14159265358979323846 /* pi */
#endif

/* Because why compile a whole .o just for this? */
#include "processing.c"
/* Default image profiles */
#include "image_profiles.c"
/* White balance cr4p */
#include "white_balance.c"

#if defined(__linux)
#include <alloca.h>
#endif

/* Easy way to deal with storing blurred images/stuff that takes ages to calculate */
processing_buffer_t * new_image_buffer()
{
    processing_buffer_t * buffer = (processing_buffer_t *)calloc(sizeof(processing_buffer_t),1);
    buffer->image = NULL;
    return buffer;
}
void buffer_set_size(processing_buffer_t * buffer, int width, int height)
{
    /* Only if it is needed */
    if (buffer->width != width || buffer->height != height)
    {
        buffer->width = width;
        buffer->height = height;
        if (buffer->image != NULL) free(buffer->image);
        buffer->image = malloc(sizeof(uint16_t) * 3 * width * height);
    }
}
uint16_t * get_buffer(processing_buffer_t * buffer)
{
    return buffer->image;
}
void free_image_buffer(processing_buffer_t * buffer)
{
    if (buffer->image != NULL) free(buffer->image);
    free(buffer);
}


processingObject_t * initProcessingObject()
{
    processingObject_t * processing = calloc( 1, sizeof(processingObject_t) );

    processing->filter = initFilterObject();

    processing->lut = init_lut();
    processing->lut_on = 0;

    /* For precalculated matrix values */
    for (int i = 0; i < 9; ++i)
    {
        processing->pre_calc_matrix[i] = malloc( 65536 * sizeof(int32_t) );
        processing->pre_calc_matrix_gradient[i] = malloc( 65536 * sizeof(int32_t) );
    }

    /* A nothing matrix */
    // processing->cam_to_sRGB_matrix[0] = 1.0;
    // processing->cam_to_sRGB_matrix[4] = 1.0;
    // processing->cam_to_sRGB_matrix[8] = 1.0;
    // /* Different matrix BTW */
    // processing->xyz_to_rgb_matrix[0] = 1.0;
    // processing->xyz_to_rgb_matrix[4] = 1.0;
    // processing->xyz_to_rgb_matrix[8] = 1.0;

    /* Blur buffer images (may change size) */
    processing->shadows_highlights.blur_image = new_image_buffer();
    buffer_set_size(processing->shadows_highlights.blur_image, 2, 2); /* Fix craxh */

    double rgb_to_YCbCr[7] = {  0.299000,  0.587000,  0.114000,
                               -0.168736, -0.331264, /* 0.5 */
                               /* 0.5 */  -0.418688, -0.081312 };
    double YCbCr_to_rgb[4] = {             1.402000,
                               -0.344136, -0.714136,
                                1.772000  };

    for (int i = 0; i < 7; ++i)
    {
        processing->cs_zone.pre_calc_rgb_to_YCbCr[i] = malloc( 65536 * sizeof(int32_t) );
        for (int j = 0; j < 65536; ++j)
        {
            processing->cs_zone.pre_calc_rgb_to_YCbCr[i][j] = (double)j * rgb_to_YCbCr[i];
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        processing->cs_zone.pre_calc_YCbCr_to_rgb[i] = malloc( 65536 * sizeof(int32_t) );
        for (int j = 0; j < 65536; ++j)
        {
            processing->cs_zone.pre_calc_YCbCr_to_rgb[i][j] = (double)(j-32768) * YCbCr_to_rgb[i];
        }
    }

    /* Gradient */
    processing->gradient_exposure_stops = 0.0;

    /* Default settings */
    processingSetWhiteBalance(processing, 6000.0, 0.0);
    processingSetBlackAndWhiteLevel(processing, 2048, 15000, 14); /* 14 bit! */
    processingSetExposureStops(processing, 0.0);
    processingSetGamma(processing, STANDARD_GAMMA);
    processingSetGammaGradient(processing, STANDARD_GAMMA);
    processingSetVibrance(processing, 1.0);
    processingSetSaturation(processing, 1.0);
    processingSetContrast(processing, 0.73, 5.175, 0.5, 0.0, 0.0);
    processingSetImageProfile(processing, PROFILE_TONEMAPPED);
    processingSetSharpening(processing, 0.0);
    processingSetHighlights(processing, 0.0);
    processingSetShadows(processing, 0.0);
    processingSetSimpleContrast(processing, 0.0);
    processingSetTransformation(processing, TR_NONE);
    processingSetDenoiserStrength(processing, 0);
    processingSetDenoiserWindow(processing, 2);
    processingSetRbfDenoiserLuma(processing, 0);
    processingSetRbfDenoiserChroma(processing, 0);
    processingSetRbfDenoiserRange(processing, 40);
    processingUseCamMatrix(processing);
    processingAllowCreativeAdjustments(processing);
    processingSetGCurve(processing, 0, NULL, NULL, 0);
    processingSetGCurve(processing, 0, NULL, NULL, 1);
    processingSetGCurve(processing, 0, NULL, NULL, 2);
    processingSetGCurve(processing, 0, NULL, NULL, 3);
    processingSetHueVsCurves(processing, 0, NULL, NULL, 0);
    processingSetHueVsCurves(processing, 0, NULL, NULL, 1);
    processingSetHueVsCurves(processing, 0, NULL, NULL, 2);
    processingSetHueVsCurves(processing, 0, NULL, NULL, 3);
    processingSetVignetteStrength(processing, 0);

    /* Colour default parameters */
    processingSetGamut(processing, GAMUT_Rec709);
    processingSetTonemappingFunction(processing, TONEMAP_Reinhard);
    processingSetGamma(processing, 3.15);
    processingSetGammaGradient(processing, 3.15);
    processingUseCamMatrix(processing);
    processingSetImageProfile(processing, PROFILE_TONEMAPPED);

    /* Just in case (should be done tho already) */
    processing_update_matrices(processing);
    //processing_update_matrices_gradient(processing);
    processing_update_shadow_highlight_curve(processing);

    processingSetToning(processing, 255, 192, 0, 0);
    processingSetCaDesaturate(processing, 0);
    processingSetCaRadius(processing, 1);

    return processing;
}


void processingSetGamut(processingObject_t * processing, int gamut)
{
    processing->colour_gamut = gamut;
    /* This will update everything necessary to enable tonemapping */
    processingSetWhiteBalance(processing, processingGetWhiteBalanceKelvin(processing), processingGetWhiteBalanceTint(processing));
    processingSetGamma(processing, processing->gamma_power);
    processingSetGammaGradient(processing, processing->gamma_power);
    processing_update_matrices(processing);
    processing_update_matrices_gradient(processing);
}

int processingGetGamut(processingObject_t * processing)
{
    return processing->colour_gamut;
}

void processingSetTonemappingFunction(processingObject_t * processing, int function)
{
    processing->tonemap_function = function;
    /* This will update everything necessary to enable tonemapping */
    processingSetGamma(processing, processing->gamma_power);
    processingSetGammaGradient(processing, processing->gamma_power);
    processing_update_matrices(processing);
    processing_update_matrices_gradient(processing);
}

int processingGetTonemappingFunction(processingObject_t * processing)
{
    return processing->tonemap_function;
}

void processingSetImageProfile(processingObject_t * processing, int imageProfile)
{
    /* Yes, we still have compatibility with old profile system */
    processingGetAllowedCreativeAdjustments(processing) = default_image_profiles[imageProfile].allow_creative_adjustments;
    processingSetGamma(processing, default_image_profiles[imageProfile].gamma_power);
    processingSetTonemappingFunction(processing, default_image_profiles[imageProfile].tonemap_function);
    processingSetGamut(processing, default_image_profiles[imageProfile].colour_gamut);

    /* This updates matrices, so new gamut will be put to use */
    processingSetWhiteBalance(processing, processingGetWhiteBalanceKelvin(processing), processingGetWhiteBalanceTint(processing));

    /* This will update everything necessary to enable tonemapping */
    processingSetGamma(processing, processing->gamma_power);
    processingSetGammaGradient(processing, processing->gamma_power);
    processing_update_matrices(processing);
    processing_update_matrices_gradient(processing);
}

/* Takes those matrices I learned about on the forum */
void processingSetCamMatrix(processingObject_t * processing, double * camMatrix, double * camMatrixA)
{
    memcpy(processing->cam_matrix, camMatrix, sizeof(double) * 9);
    memcpy(processing->cam_matrix_A, camMatrixA, sizeof(double) * 9);
    /* TO update matrices really argh so much confusion :( */
    processingSetWhiteBalance(processing, processingGetWhiteBalanceKelvin(processing), processingGetWhiteBalanceTint(processing));
    /* Calculates final main matrix */
    processing_update_matrices(processing);
    if( processing->gradient_enable != 0 ) processing_update_matrices_gradient(processing);
}

void processingSetHighlights(processingObject_t * processing, double value)
{
    processing->shadows_highlights.highlights = value;
    processing_update_shadow_highlight_curve(processing);
}
void processingSetShadows(processingObject_t * processing, double value)
{
    processing->shadows_highlights.shadows = value;
    processing_update_shadow_highlight_curve(processing);
}

void processing_update_shadow_highlight_curve(processingObject_t * processing)
{
    double shadows_expo = processing->shadows_highlights.shadows;
    double highlight_expo = pow(2.0, processing->shadows_highlights.highlights*(-1.5));
    #pragma omp parallel for
    for (int i = 1; i < 65536; ++i)
    {
        double expo_factor;
        double value = pow(((double)i)/65536.0, 0.75);

        double newvalue = add_contrast(value, 0.7, shadows_expo*0.5, 0.0, 0.0);

        if (newvalue > 0.2) {
            double fac = (newvalue - 0.2)*(1/0.8);
            newvalue = newvalue*(1.0-fac) + newvalue*highlight_expo*fac;
        }

        expo_factor = value/newvalue;

        processing->shadows_highlights.shadow_highlight_curve[i] = expo_factor;
    }
    processing->shadows_highlights.shadow_highlight_curve[0] = 1.0;
}

void processingSetSimpleContrast(processingObject_t * processing, double value)
{
    processing->contrast = value * 0.65;
    processing_update_contrast_curve(processing);
}

void processing_update_contrast_curve(processingObject_t * processing)
{
    double shadows_expo = -processing->contrast;
    double highlight_expo = pow(2.0, processing->contrast*(-1.5));
    #pragma omp parallel for
    for (int i = 1; i < 65536; ++i)
    {
        double expo_factor;
        double value = pow(((double)i)/65536.0, 0.75);

        double newvalue = add_contrast(value, 0.7, shadows_expo*0.5, 0.0, 0.0);

        if (newvalue > 0.2) {
            double fac = (newvalue - 0.2)*(1/0.8);
            newvalue = newvalue*(1.0-fac) + newvalue*highlight_expo*fac;
        }

        expo_factor = value/newvalue;

        processing->contrast_curve[i] = expo_factor;
    }
    processing->contrast_curve[0] = 1.0;
}

void processingSetSimpleContrastGradient(processingObject_t * processing, double value)
{
    processing->gradient_contrast = value * 0.65;
    processing_update_contrast_curve_gradient(processing);
}

void processing_update_contrast_curve_gradient(processingObject_t * processing)
{
    double shadows_expo = -processing->gradient_contrast;
    double highlight_expo = pow(2.0, processing->gradient_contrast*(-1.5));
    #pragma omp parallel for
    for (int i = 1; i < 65536; ++i)
    {
        double expo_factor;
        double value = pow(((double)i)/65536.0, 0.75);

        double newvalue = add_contrast(value, 0.7, shadows_expo*0.5, 0.0, 0.0);

        if (newvalue > 0.2) {
            double fac = (newvalue - 0.2)*(1/0.8);
            newvalue = newvalue*(1.0-fac) + newvalue*highlight_expo*fac;
        }

        expo_factor = value/newvalue;

        processing->gradient_contrast_curve[i] = expo_factor;
    }
    processing->gradient_contrast_curve[0] = 1.0;
}

void processingSetClarity(processingObject_t * processing, double value)
{
    if( value < 0 ) value /= 2.0;
    processing->clarity = value;
    processing_update_clarity_curve(processing);
}

void processing_update_clarity_curve(processingObject_t * processing)
{
    double shadows_expo = -processing->clarity;
    double highlight_expo = pow(2.0, processing->clarity*(-1.5));
    #pragma omp parallel for
    for (int i = 1; i < 65536; ++i)
    {
        double expo_factor;
        double value = pow(((double)i)/65536.0, 0.75);

        double newvalue = add_contrast(value, 0.7, shadows_expo*0.5, 0.0, 0.0);

        if (newvalue > 0.2) {
            double fac = (newvalue - 0.2)*(1/0.8);
            newvalue = newvalue*(1.0-fac) + newvalue*highlight_expo*fac;
        }

        expo_factor = value/newvalue;

        processing->clarity_curve[i] = expo_factor;
    }
    processing->clarity_curve[0] = 1.0;
}

/* applyProcessingObject but with one argument for pthreading  */
void processing_object_thread(apply_processing_parameters_t * p)
{
    apply_processing_object( p->processing, 
                             p->imageX, p->imageY, 
                             p->inputImage, 
                             p->outputImage,
                             p->blurImage,
                             p->gradientMask,
                             p->vignetteMask );
}

/* Apply it with multiple threads */
void applyProcessingObject( processingObject_t * processing, 
                            int imageX, int imageY, 
                            uint16_t * __restrict inputImage, 
                            uint16_t * __restrict outputImage,
                            int threads, int imageChanged, uint64_t frameIndex )
{
    /* Do transformation */
    get_frame_transformed(processing, inputImage, imageX, imageY);

    int img_s = imageX * imageY * 3;
    //Noise shall not move for the same picture
    uint32_t randomseed1 = ((uint32_t *)inputImage)[0] ^ ((uint32_t *)(inputImage+img_s))[-1] ^ frameIndex;
    uint32_t randomseed2 = ((uint32_t *)inputImage)[1] ^ ((uint32_t *)(inputImage+img_s/2))[0] ^ frameIndex;
    uint32_t randomseed3 = ((uint32_t *)inputImage)[2] ^ ((uint32_t *)(inputImage+img_s/3))[0] ^ frameIndex;
    uint32_t randomseed4 = ((uint32_t *)inputImage)[3] ^ ((uint32_t *)(inputImage+img_s/4))[0] ^ frameIndex;

    /* Resize image buffer to make sure its right size */
    if (imageChanged) buffer_set_size(processing->shadows_highlights.blur_image, imageX, imageY);

    if (imageChanged) memcpy(get_buffer(processing->shadows_highlights.blur_image), inputImage, imageX * imageY * sizeof(uint16_t) * 3);

    /* If shadows/highlights off don't do anything. Maybe this blurring bit could b multithreaded I need to think */
    if( ( processing->shadows_highlights.shadows <= -0.01 || processing->shadows_highlights.shadows >= 0.01 )
     || ( processing->shadows_highlights.highlights <= -0.01 || processing->shadows_highlights.highlights >= 0.01 )
     || ( processing->clarity <= -0.01 || processing->clarity >= 0.01 ) )
    {

        /* Blur diameter depends on image diagonal */
        int blur_radius = (int)(((sqrt(pow(imageX,2.0)+pow(imageY,2.0)) / 440.0 - 1.0)/2 + 0.5)*4.0);

        /* Reblur if image changed */
        if (imageChanged)
        {
            //memcpy(get_buffer(processing->shadows_highlights.blur_image), inputImage, imageX * imageY * sizeof(uint16_t) * 3);
            //blur_image(get_buffer(processing->shadows_highlights.blur_image), outputImage, imageX, imageY, blur_radius, 1, 1, 1, 0, imageY-1);
            if(0) blur_image_threaded( get_buffer(processing->shadows_highlights.blur_image), outputImage, imageX, imageY, blur_radius, threads );
            else
                recursive_bf_wrap(
                        inputImage,
                        get_buffer(processing->shadows_highlights.blur_image),
                        0.0005f, 0.075f+(((float)100.0-40.0f)/666.6f),
                        imageX, imageY, 3);

            /* Apply basic levels */
            int img_s = imageX * imageY * 3;
            uint16_t * img = get_buffer(processing->shadows_highlights.blur_image);
            #pragma omp parallel for
            for (int i = 0; i < img_s; ++i) img[i] = processing->pre_calc_levels[ img[i] ];
        }
    }

    /* Analyse dual iso frame to find highest green for highlight reconstruction */
    analyse_frame_highest_green( processing, imageX, imageY, inputImage );

    /* If threads is 1, no threads are needed */
    if (threads == 1)
    {
        apply_processing_object(processing, imageX, imageY, inputImage, outputImage, get_buffer(processing->shadows_highlights.blur_image), processing->gradient_mask, processing->vignette_mask);
    }
    else
    {
        apply_processing_parameters_t * params = alloca(sizeof(apply_processing_parameters_t) * threads);

        /* All chunks this height except possibly slightly longer last one */
        int chunk_size = imageY/threads;
        /* Size of a chunk */
        uint32_t offset_chunk = imageX * chunk_size * 3;

        /* Split in to chunks for each thread */
        for (int t = 0; t < threads; ++t)
        {
            params[t].processing = processing;
            params[t].imageX = imageX;
            params[t].imageY = chunk_size;
            params[t].inputImage = inputImage + offset_chunk*t;
            params[t].outputImage = outputImage + offset_chunk*t;
            params[t].blurImage = get_buffer(processing->shadows_highlights.blur_image) + offset_chunk*t;
            params[t].gradientMask = processing->gradient_mask + (imageX * chunk_size * t);
            params[t].vignetteMask = processing->vignette_mask + (imageX * chunk_size * t);
        }

        /* To make sure bottom is processed */
        params[threads-1].imageY = imageY - chunk_size * (threads-1);

        pthread_t * threadid = alloca(threads * sizeof(pthread_t));

        /* Do threads */
        for (int t = 0; t < threads; ++t)
        {
            pthread_create(&threadid[t], NULL, (void *)&processing_object_thread, (void *)(params + t));
        }
        /* let all threads finish */
        for (int t = 0; t < threads; ++t)
        {
            pthread_join(threadid[t], NULL);
        }
    }

    /* Denoiser must render on complete image, because of 2D median border problem */
    if( processing->denoiserStrength > 0 )
    {
        denoise_2D_median( outputImage, imageX, imageY, processing->denoiserWindow, processing->denoiserStrength );
    }

    /* Recursive bilateral filtering (developed by Qingxiong Yang) must render on complete image, because of border problems */
    if( processing->rbfDenoiserLuma > 0 || processing->rbfDenoiserChroma > 0 )
    {
        int img_s = imageX * imageY * 3;
        memcpy( inputImage, outputImage, img_s * sizeof(uint16_t) );
        recursive_bf_wrap(
                inputImage,
                outputImage,
                0.0025f, 0.075f+(((float)processing->rbfDenoiserRange-40.0f)/666.6f),
                imageX, imageY, 3);

        float outL = processing->rbfDenoiserLuma/100.0;
        float inL = 1.0 - outL;
        float outC = processing->rbfDenoiserChroma/100.0;
        float inC = 1.0 - outC;

        convert_rgb_to_YCbCr_omp(inputImage, img_s, processing->cs_zone.pre_calc_rgb_to_YCbCr);
        convert_rgb_to_YCbCr_omp(outputImage, img_s, processing->cs_zone.pre_calc_rgb_to_YCbCr);
        /* Linear blend strengths */
#pragma omp parallel for
        for( int i = 0; i < img_s; i+=3 )
        {
            outputImage[i+0] = outputImage[i+0]*outL + inputImage[i+0]*inL;
            outputImage[i+1] = outputImage[i+1]*outC + inputImage[i+1]*inC;
            outputImage[i+2] = outputImage[i+2]*outC + inputImage[i+2]*inC;
        }
        convert_YCbCr_to_rgb_omp(outputImage, img_s, processing->cs_zone.pre_calc_YCbCr_to_rgb);
    }
    /* RGB CA&ColorMoiree Removal */
    if( processing->ca_desaturate > 0 )
    {
        int img_s = imageX * imageY * 3;
        memcpy( inputImage, outputImage, img_s * sizeof(uint16_t) );
        CACorrection(imageX, imageY, inputImage, outputImage,
                     (uint16_t)(100-processing->ca_desaturate)<<9,
                     processing->ca_radius);
    }
    /* Grain (simple monochrome noise) generator - must be applied after denoiser */
    if( processing->grainStrength > 0 ) //Switch on/off
    {
        int strength = 50 * processing->grainStrength;
        for( int i = 0; i < img_s; i+=3 )
        {
            uint32_t randomval = randomseed1 ^ ((i*randomseed2) * (randomseed3-i) * (i+randomseed4));
            int grain = ( randomval % strength ) - ( strength >> 2 ); //change value for strength
            outputImage[i+0] = LIMIT16( outputImage[i+0] + grain );
            outputImage[i+1] = LIMIT16( outputImage[i+1] + grain );
            outputImage[i+2] = LIMIT16( outputImage[i+2] + grain );
        }
    }
}

/* A private part of the processing machine */
void apply_processing_object( processingObject_t * processing,
                              int imageX, int imageY, 
                              uint16_t * __restrict inputImage, 
                              uint16_t * __restrict outputImage,
                              uint16_t * __restrict blurImage,
                              uint16_t * __restrict gradientMask,
                              float * __restrict vignetteMask )
{
    /* Number of elements */
    int img_s = imageX * imageY * 3;

    /* (for shorter code) */
    int32_t ** pm = processing->pre_calc_matrix;
    int32_t ** pmg = processing->pre_calc_matrix_gradient;
    uint16_t * out_img = outputImage;
    uint16_t * img = inputImage;
    uint16_t * img_end = img + img_s;
    uint16_t * gm = gradientMask;
    float * vm = vignetteMask;
    float * vmpix = vm;

    /* In case of camera matrix */
    double (* tone_mapping_function)(double) = tonemap_functions[processing->tonemap_function];

    /* Apply some precalcuolated settings */
    for (int i = 0; i < img_s; ++i)
    {
        /* Black + white level */
        img[i] = processing->pre_calc_levels[ img[i] ];
    }

    /* white balance & exposure & highlights & gamma & highlight reconstruction */
    for (uint16_t * pix = img, * bpix = blurImage, *gmpix = gm; pix < img_end; pix += 3, bpix += 3, gmpix++)
    {
        double expo_correction = 1.0;
        double expo_correction_gradient = 1.0;

        /* Vignette correction */
        if( processing->vignette_strength != 0 )
        {
            vmpix++;
            if( vmpix < processing->vignette_end )  /* just safety - sometimes parameters may change faster than processing */
            {
                expo_correction *= pow( 1.0 + ( vmpix[0] * processing->vignette_strength / 128.0 ), 4 );
            }
        }

        if (processing->allow_creative_adjustments)
        {
            /* shadows & highlights, clarity part 1 */
            if( ( processing->shadows_highlights.shadows    <= -0.01 || processing->shadows_highlights.shadows    >= 0.01 )
            || ( processing->shadows_highlights.highlights <= -0.01 || processing->shadows_highlights.highlights >= 0.01 )
            || ( processing->clarity                       <= -0.01 || processing->clarity                       >= 0.01 ) )
            {
                /* Blur pixLZ */
                int32_t bval = ( ((pm[0][bpix[0]] /* + pm[1][bpix[1]] + pm[2][bpix[2]] */) << 2)
                            + ((/* pm[3][bpix[0]] + */ pm[4][bpix[1]] /* + pm[5][bpix[2]] */) * 11)
                            +  (/* pm[6][bpix[0]] + pm[7][bpix[1]] + */ pm[8][bpix[2]]) ) >> 4;

                if( processing->clarity <= -0.01 || processing->clarity >= 0.01 )
                {
                    /* clarity part 1 */
                    double factor = processing->clarity_curve[LIMIT16(bval)];
                    expo_correction /= (factor * factor);
                }
                if( ( processing->shadows_highlights.shadows <= -0.01 || processing->shadows_highlights.shadows >= 0.01 )
                || ( processing->shadows_highlights.highlights <= -0.01 || processing->shadows_highlights.highlights >= 0.01 ) )
                {
                    /* highlight exposure factor */
                    expo_correction *= processing->shadows_highlights.shadow_highlight_curve[LIMIT16(bval)];
                }
            }

            /* Contrast on untouched pixel */
            if( ( processing->contrast          <= -0.01 || processing->contrast          >= 0.01 )
            || ( processing->clarity           <= -0.01 || processing->clarity           >= 0.01 )
            || ( processing->gradient_contrast <= -0.01 || processing->gradient_contrast >= 0.01 ) )
            {
                int32_t cval = ( ((pm[0][pix[0]] /* + pm[1][pix[1]] + pm[2][pix[2]] */) << 2)
                             + ((/* pm[3][pix[0]] + */ pm[4][pix[1]] /* + pm[5][pix[2]] */) * 11)
                             +  (/* pm[6][pix[0]] + pm[7][pix[1]] + */ pm[8][pix[2]]) ) >> 4;

                if( processing->clarity <= -0.01 || processing->clarity >= 0.01 )
                {
                    /* clarity part 2 */
                    double factor = processing->clarity_curve[LIMIT16(cval)];
                    expo_correction *= factor * factor;
                }
                if( processing->contrast <= -0.01 || processing->contrast >= 0.01 )
                {
                    /* contrast factor */
                    expo_correction *= processing->contrast_curve[LIMIT16(cval)];
                }
                if( processing->gradient_contrast <= -0.01 || processing->gradient_contrast >= 0.01 )
                {
                    /* gradient contrast factor */
                    expo_correction_gradient *= processing->gradient_contrast_curve[LIMIT16(cval)];
                }
            }
        }

        /* white balance & exposure */
        int32_t pix0 = (pm[0][pix[0]] /* + pm[1][pix[1]] + pm[2][pix[2]] */)*expo_correction;
        int32_t pix1 = (/* pm[3][pix[0]] + */ pm[4][pix[1]] /* + pm[5][pix[2]] */)*expo_correction;
        int32_t pix2 = (/* pm[6][pix[0]] + pm[7][pix[1]] + */ pm[8][pix[2]])*expo_correction;
        int32_t tmp1 = (/* pm[3][pix[0]] + */ pm[4][pix[1]] /* + pm[5][pix[2]] */);

        /* Gradient variables and part 1 */
        uint16_t pixg[3];
        if( processing->gradient_enable && gmpix[0] != 0 &&
          ( ( processing->gradient_exposure_stops < -0.01 || processing->gradient_exposure_stops > 0.01 )
         || ( processing->gradient_contrast       < -0.01 || processing->gradient_contrast       > 0.01 ) ) )
        {
            /* do the same for gradient as for the pic itself, but before the values are overwritten */
            /* white balance & exposure */
            int32_t pix0g = (pmg[0][pix[0]] /* + pmg[1][pix[1]] + pmg[2][pix[2]] */) * expo_correction * expo_correction_gradient;
            int32_t pix1g = (/* pmg[3][pix[0]] + */ pmg[4][pix[1]] /* + pmg[5][pix[2]] */) * expo_correction * expo_correction_gradient;
            int32_t pix2g = (/* pmg[6][pix[0]] + pmg[7][pix[1]] */ + pmg[8][pix[2]]) * expo_correction * expo_correction_gradient;
            int32_t tmp1g = (/* pmg[3][pix[0]] + */ pmg[4][pix[1]] /* + pmg[5][pix[2]] */);

            pixg[0] = LIMIT16(pix0g);
            pixg[1] = LIMIT16(pix1g);
            pixg[2] = LIMIT16(pix2g);
            tmp1g   = LIMIT16(tmp1g);

            /* Now highlight reconstruction for gradient layer*/
            if (processing->highlight_reconstruction)
            {
                if(*processing->dual_iso != 0)
                {
                    /* Check if its the range of highest green value possible */
                    /* the range makes it cleaner against pink noise */
                    if (tmp1g >= LIMIT16( processing->highest_green_gradient_diso - 5000 ) && tmp1g <= LIMIT16( processing->highest_green_gradient_diso + 5000 ))
                    {
                        if( pixg[1] < 1.1*pixg[0] && pixg[1] < pixg[2] )
                        {
                            pixg[1] = (pixg[0] + pixg[2]) / 2;
                        }
                    }
                }
                else
                {
                    /* Check if its the highest green value possible */
                    if (tmp1g == processing->highest_green_gradient)
                    {
                        pixg[1] = (pixg[0] + pixg[2]) / 2;
                    }
                }
            }
        }

        pix[0] = LIMIT16(pix0);
        pix[1] = LIMIT16(pix1);
        pix[2] = LIMIT16(pix2);
        tmp1   = LIMIT16(tmp1);

        /* Now highlight reconstruction */
        if (processing->highlight_reconstruction)
        {
            if(*processing->dual_iso != 0)
            {
                /* Check if its the range of highest green value possible */
                /* the range makes it cleaner against pink noise */
                if (tmp1 >= LIMIT16( processing->highest_green_diso - 5000 ) && tmp1 <= LIMIT16( processing->highest_green_diso + 5000 ))
                {
                    if( pix[1] < 1.1*pix[0] && pix[1] < pix[2] )
                    {
                        pix[1] = (pix[0] + pix[2]) / 2;
                    }
                }
            }
            else
            {
                /* Check if its the highest green value possible */
                if (tmp1 == processing->highest_green)
                {
                    pix[1] = (pix[0] + pix[2]) / 2;
                }
                /* Aggressive mode */
                /*if (tmp1b >= processing->highest_green - 15000 && tmp1b <= processing->highest_green)
                {
                    if( pix[1] < 1.1*pix[0] && pix[1] < pix[2] )
                    {
                        pix[1] = (pix[0] + pix[2]) / 2;
                    }
                }*/
            }
        }

        /* I really don't like how this if is in a big loop :(( */
        if( processing->use_cam_matrix > 0 )
        {
            /* WB correction */
            uint16_t pix0b = pix[0], pix1b = pix[1], pix2b = pix[2];
            double result[3];
            result[0] = pix0b * processing->proper_wb_matrix[0] + pix1b * processing->proper_wb_matrix[1] + pix2b * processing->proper_wb_matrix[2];
            result[1] = pix0b * processing->proper_wb_matrix[3] + pix1b * processing->proper_wb_matrix[4] + pix2b * processing->proper_wb_matrix[5];
            result[2] = pix0b * processing->proper_wb_matrix[6] + pix1b * processing->proper_wb_matrix[7] + pix2b * processing->proper_wb_matrix[8];

            pix[0] = LIMIT16(result[0]);
            pix[1] = LIMIT16(result[1]);
            pix[2] = LIMIT16(result[2]);
        }

        /* Gamma and expo correction (shadows&highlights, contrast, clarity)*/
        for( int i = 0; i < 3; i++ )
        {
            pix[i] = processing->pre_calc_gamma[ pix[i] ];
        }

        /* Gradient part 2 & blending */
        if( processing->gradient_enable && gmpix[0] != 0 &&
          ( ( processing->gradient_exposure_stops < -0.01 || processing->gradient_exposure_stops > 0.01 )
         || ( processing->gradient_contrast       < -0.01 || processing->gradient_contrast       > 0.01 ) ) )
        {
            /* WB correction gradient layer*/
            if( processing->use_cam_matrix > 0 )
            {
                uint16_t pix0b = pixg[0], pix1b = pixg[1], pix2b = pixg[2];
                double result[3];
                result[0] = pix0b * processing->proper_wb_matrix[0] + pix1b * processing->proper_wb_matrix[1] + pix2b * processing->proper_wb_matrix[2];
                result[1] = pix0b * processing->proper_wb_matrix[3] + pix1b * processing->proper_wb_matrix[4] + pix2b * processing->proper_wb_matrix[5];
                result[2] = pix0b * processing->proper_wb_matrix[6] + pix1b * processing->proper_wb_matrix[7] + pix2b * processing->proper_wb_matrix[8];
                pixg[0] = LIMIT16(result[0]);
                pixg[1] = LIMIT16(result[1]);
                pixg[2] = LIMIT16(result[2]);
            }

            /* Gamma and expo correction (shadows&highlights, contrast, clarity) gradient layer*/
            for( int i = 0; i < 3; i++ )
            {
                pixg[i] = processing->pre_calc_gamma_gradient[ pixg[i] ];
            }

            /* Blending using the mask */
            pix[0] = gmpix[0] / 65535.0 * pixg[0] + (65535 - gmpix[0]) / 65535.0 * pix[0];
            pix[1] = gmpix[0] / 65535.0 * pixg[1] + (65535 - gmpix[0]) / 65535.0 * pix[1];
            pix[2] = gmpix[0] / 65535.0 * pixg[2] + (65535 - gmpix[0]) / 65535.0 * pix[2];
        }
    }

    //Code for HueVs...
    if( (processing->allow_creative_adjustments )
     && ( processing->hue_vs_luma_used
       || processing->hue_vs_saturation_used
       || processing->hue_vs_hue_used
       || processing->luma_vs_saturation_used ) )
    {
        for (uint16_t * pix = img; pix < img_end; pix += 3)
        {
            float hsl[3];
            float rgb[3];
            for( int i = 0; i < 3; i++ ) rgb[i] = pix[i] / 65535.0f;
            fromRGBtoHSV( rgb, hsl );
            //rgb_to_hsl( pix, hsl );

            /* Calculate saturation value of untouched pixel (taken from vibrance, gives better results than from rgb_to_hsl) */
            // ///////////////////////
            double sat = 0;
            if( !( pix[0] == 0 && pix[1] == 0 && pix[2] == 0 ) )
            {
                uint16_t biggest = 0;
                uint16_t smallest = 65535;
                for( int i = 0; i < 3; i++ )
                {
                    if( pix[i] > biggest ) biggest = pix[i];
                    if( pix[i] < smallest ) smallest = pix[i];
                }
                sat = ((double)biggest - (double)smallest) / (double)biggest;
            }
            /* Some cheat factor to make the effect more visible */
            sat = 2.0 * sat / ( sat * sat + 1 );
            if( sat > 1.0 ) sat = 1.0;
            // ///////////////////////

            uint16_t hue = (uint16_t)(hsl[0] * 100.0);

            hsl[2] *= 1.0 + (processing->hue_vs_luma[hue] * sat * 2);
            if( hsl[2] < 0.0 ) hsl[2] = 0.0;
            //if( hsl[2] > 1.0 ) hsl[2] = 1.0;

            hsl[1] *= 1.0 + (processing->hue_vs_saturation[hue] * 2);
            if( hsl[1] < 0.0 ) hsl[1] = 0.0;
            //if( hsl[1] > 1.0 ) hsl[1] = 1.0;

            hsl[0] += 60 * processing->hue_vs_hue[hue];
            if( hsl[0] < 0 ) hsl[0] += 360;
            else if( hsl[0] >= 360 ) hsl[0] -= 360;

            uint16_t luma = (uint16_t)((hsl[2]) * 36000.0);
            hsl[1] *= 1.0 + (processing->luma_vs_saturation[luma] * 2);
            if( hsl[1] < 0.0 ) hsl[1] = 0.0;

            //hsl_to_rgb( hsl, pix );
            fromHSVtoRGB( hsl, rgb );
            for( int i = 0; i < 3; i++ ) pix[i] = LIMIT16( rgb[i] * 65535.0f + 0.5f );
        }
    }

    if (processing->allow_creative_adjustments)
    {
        if( processing->vibrance > 1.01 || processing->vibrance < 0.99 )
        {
            /* Now vibrance, before saturation, because we need untouched colors (in terms of saturation) */
            for (uint16_t * pix = img; pix < img_end; pix += 3)
            {
                /* Pixel brightness = 4/16 R, 11/16 G, 1/16 blue; Try swapping the channels, it will look worse */
                int32_t Y1 = ((pix[0] << 2) + (pix[1] * 11) + pix[2]) >> 4;
                int32_t Y2 = Y1 - 65536;

                /* Increase difference between channels and the saturation midpoint */
                int32_t pix0 = processing->pre_calc_vibrance[pix[0] - Y2] + Y1;
                int32_t pix1 = processing->pre_calc_vibrance[pix[1] - Y2] + Y1;
                int32_t pix2 = processing->pre_calc_vibrance[pix[2] - Y2] + Y1;

                /* Positive vibrance in dependency to raw saturation */
                if( processing->vibrance > 1.0 )
                {
                    /* Calculate saturation value of untouched pixel */
                    double sat = 0;
                    if( !( pix[0] == 0 && pix[1] == 0 && pix[2] == 0 ) )
                    {
                        uint16_t biggest = 0;
                        uint16_t smallest = 65535;
                        for( int i = 0; i < 3; i++ )
                        {
                            if( pix[i] > biggest ) biggest = pix[i];
                            if( pix[i] < smallest ) smallest = pix[i];
                        }
                        sat = ((double)biggest - (double)smallest) / (double)biggest;
                    }
                    /* Some cheat factor to make the effect more visible */
                    sat = 2.0 * sat / ( sat * sat + 1 );
                    if( sat > 1.0 ) sat = 1.0;
                    /* The less saturated the pixel was, the more saturation it gets */
                    pix[0] = LIMIT16( pix[0] * sat + pix0 * ( 1.0 - sat ) );
                    pix[1] = LIMIT16( pix[1] * sat + pix1 * ( 1.0 - sat ) );
                    pix[2] = LIMIT16( pix[2] * sat + pix2 * ( 1.0 - sat ) );
                }
                /* Negative vibrance is the same as (un)saturation */
                else
                {
                    pix[0] = LIMIT16(pix0);
                    pix[1] = LIMIT16(pix1);
                    pix[2] = LIMIT16(pix2);
                }
            }
        }

        if( processing->saturation > 1.01 || processing->saturation < 0.99 )
        {
            /* Now saturation (looks way better after gamma) */
            for (uint16_t * pix = img; pix < img_end; pix += 3)
            {
                /* Pixel brightness = 4/16 R, 11/16 G, 1/16 blue; Try swapping the channels, it will look worse */
                int32_t Y1 = ((pix[0] << 2) + (pix[1] * 11) + pix[2]) >> 4;
                int32_t Y2 = Y1 - 65536;

                /* Increase difference between channels and the saturation midpoint */
                int32_t pix0 = processing->pre_calc_sat[pix[0] - Y2] + Y1;
                int32_t pix1 = processing->pre_calc_sat[pix[1] - Y2] + Y1;
                int32_t pix2 = processing->pre_calc_sat[pix[2] - Y2] + Y1;

                pix[0] = LIMIT16(pix0);
                pix[1] = LIMIT16(pix1);
                pix[2] = LIMIT16(pix2);
            }
        }
    }

    /* Toning */
    if (processing->allow_creative_adjustments)
    {
        if( processing->toning_dry < 99.8 )
        {
            for (uint16_t * pix = img; pix < img_end; pix += 3)
            {
                for( int i = 0; i < 3; i++ )
                {
                    pix[i] = pix[i] * processing->toning_dry + pix[i] * processing->toning_wet[i];
                }
            }
        }
    }

    if (processing->allow_creative_adjustments)
    {
        /* Contrast Curve (OMG putting this after gamma made it 999x better) */
        for (uint16_t * pix = img; pix < img_end; pix += 3)
        {
            pix[0] = processing->pre_calc_curve_r[ pix[0] ];
            pix[1] = processing->pre_calc_curve_r[ pix[1] ];
            pix[2] = processing->pre_calc_curve_r[ pix[2] ];
        }
    }

    if (processing->allow_creative_adjustments)
    {
        //Gradation curve
        for (uint16_t * pix = img; pix < img_end; pix += 3)
        {
            pix[0] = processing->gcurve_y[ pix[0] ];
            pix[1] = processing->gcurve_y[ pix[1] ];
            pix[2] = processing->gcurve_y[ pix[2] ];
            pix[0] = processing->gcurve_r[ pix[0] ];
            pix[1] = processing->gcurve_g[ pix[1] ];
            pix[2] = processing->gcurve_b[ pix[2] ];
        }
    }

    uint32_t sharp_skip = 1; /* Skip how many pixels when applying sharpening */
    uint32_t sharp_start = 0; /* How many pixels offset to start at */

    /* enter YCbCr world - https://en.wikipedia.org/wiki/YCbCr (I used the 'JPEG Transform') */
    if (processingUsesChromaSeparation(processing))
    {
        convert_rgb_to_YCbCr(img, img_s, processing->cs_zone.pre_calc_rgb_to_YCbCr);

        sharp_start = 0; /* Start at 0 - Luma/Y channel */
        sharp_skip = 3; /* Only sharpen every third (Y/luma) pixel */
    }

    /* Basic box blur */
    if (processingGetChromaBlurRadius(processing) > 0 && processingUsesChromaSeparation(processing))
    {
        memcpy(out_img, img, img_s * sizeof(uint16_t));
        blur_image( img, out_img,
                    imageX, imageY, processingGetChromaBlurRadius(processing),
                    0,1,1,
                    0,0 );
    }

    if (processingGetSharpening(processing) > 0.005)
    {
        /* Use sobel filter to create a edge mask */
        uint16_t *gray,
             *sobel_h_res,
             *sobel_v_res,
             *contour_img;
        if( processing->sh_masking > 0 ) sobelFilter( inputImage, &gray, &sobel_h_res, &sobel_v_res, &contour_img, imageX, imageY );

        /* Avoid gaps in pixels if skipping pixels during sharpen */
        if (sharp_skip != 1) memcpy(outputImage, inputImage, img_s * sizeof(uint16_t));
    
        uint32_t y_max = imageY - 1;
        uint32_t x_max = (imageX - 1) * 3; /* X in multiples of 3 for RGB */

        /* Center and outter lut */
        uint32_t * ka = processing->pre_calc_sharp_a;
        uint16_t * kx = processing->pre_calc_sharp_x;
        uint16_t * ky = processing->pre_calc_sharp_y;
        
        /* Row length elements */
        uint32_t rl = imageX * 3;

        for (uint32_t y = 1; y < y_max; ++y)
        {
            uint16_t * out_row = out_img + (y * rl); /* current row ouptut */
            uint16_t * row = img + (y * rl); /* current row */
            uint16_t * p_row = img + ((y-1) * rl); /* previous */
            uint16_t * n_row = img + ((y+1) * rl); /* next */
            uint16_t * cont_row;
            if( processing->sh_masking > 0 )
            {
                cont_row = contour_img + (y * imageX);
            }

            for (uint32_t x = 3+sharp_start; x < x_max; x+=sharp_skip)
            {
                int32_t sharp = ka[row[x]] 
                              - ky[p_row[x]]
                              - ky[n_row[x]]
                              - kx[row[x-3]]
                              - kx[row[x+3]];

                /* use the edge mask for sharpening only edges */
                if( processing->sh_masking > 0 )
                {
                    uint32_t x1 = x / 3;
                    /* more contrast & brightness for mask */
                    uint32_t maskIntensity = 15000;
                    uint32_t cont = cont_row[x1] + (100-(uint32_t)processing->sh_masking) * 150;
                    if( cont > maskIntensity ) cont = maskIntensity;
                    /* calc output in dependency to mask slider */
                    out_row[x] = LIMIT16( ( cont / (float)maskIntensity) * LIMIT16(sharp)
                                      + ( ( maskIntensity - cont ) / (float)maskIntensity ) * row[x] );
                    /* Show mask */
                    //out_row[x] = LIMIT16(cont/(float)maskIntensity*65535.0);
                }
                /* sharpen all */
                else
                {
                    out_row[x] = LIMIT16(sharp);
                }
            }

            /* Edge pixels (basically don't do any changes to them) */
            out_row[0] = row[0];
            out_row[1] = row[1];
            out_row[2] = row[2];
            out_row += rl;
            row += rl;
            out_row[-3] = row[-3];
            out_row[-2] = row[-2];
            out_row[-1] = row[-1];
        }

        /* Copy top and bottom row */
        memcpy(outputImage, inputImage, rl * sizeof(uint16_t));
        memcpy(outputImage + (rl*(imageY-1)), inputImage + (rl*(imageY-1)), rl * sizeof(uint16_t));

        if( processing->sh_masking > 0 )
        {
            if( gray ) free( gray );
            if( sobel_h_res ) free( sobel_h_res );
            if( sobel_v_res ) free( sobel_v_res );
            if( contour_img ) free( contour_img );
        }
    }
    else
    {
        memcpy(outputImage, inputImage, img_s * sizeof(uint16_t));
    }

    /* Leave Y-Cb-Cr world */
    if (processingUsesChromaSeparation(processing))
    {
        convert_YCbCr_to_rgb(outputImage, img_s, processing->cs_zone.pre_calc_YCbCr_to_rgb);
    }

    if (processing->lut_on)
    {
        apply_lut( processing->lut, imageX, imageY, outputImage );
    }

    if (processing->filter_on)
    {
        applyFilterObject(processing->filter, imageX, imageY, outputImage);
    }
}

/* Pass frame buffer and do the transform on it */
void get_frame_transformed(processingObject_t *processing, uint16_t * frame_buf, uint16_t imageX, uint16_t imageY)
{
    if(processing->transformation == TR_ROT180)
    {
        int half_pixels = imageX * imageY / 2;
        int frame_size = imageX * imageY * sizeof(uint16_t) * 3;

        uint8_t rgb_pixel[6];
        uint8_t * rgb_buf = (uint8_t*)frame_buf;
        for(int i = 0; i < half_pixels; ++i)
        {
            memcpy(rgb_pixel, rgb_buf + i*6, 6);
            memcpy(rgb_buf + i*6, rgb_buf + frame_size - 6 - i*6, 6);
            memcpy(rgb_buf + frame_size - 6 - i*6, rgb_pixel, 6);
        }
    }
}

/* Set contrast (S-curve really) */
void processingSetContrast( processingObject_t * processing, 
                            double DCRange,  /* Dark contrast range: 0.0 to 1.0 */
                            double DCFactor, /* Dark contrast strength: 0.0 to 8.0(any range really) */
                            double LCRange,  /* Light contrast range */
                            double LCFactor, /* Light contrast strength */
                            double lighten   /* 0-1 (for good highlight rolloff) */ )
{
    /* Basic things */
    processing->light_contrast_factor = LCFactor;
    processing->light_contrast_range = LCRange;
    processing->dark_contrast_factor = DCFactor;
    processing->dark_contrast_range = DCRange;
    processing->lighten = lighten;

    processing_update_curves(processing);
}

void processingSetDCRange(processingObject_t * processing, double DCRange)
{
    processing->dark_contrast_range = DCRange;
    processing_update_curves(processing);
}
void processingSetDCFactor(processingObject_t * processing, double DCFactor)
{
    processing->dark_contrast_factor = DCFactor;
    processing_update_curves(processing);
}
void processingSetLCRange(processingObject_t * processing, double LCRange) 
{
    processing->light_contrast_range = LCRange;
    processing_update_curves(processing);
}
void processingSetLCFactor(processingObject_t * processing, double LCFactor)
{
    processing->light_contrast_factor = LCFactor;
    processing_update_curves(processing);
}
void processingSetLightening(processingObject_t * processing, double lighten)
{
    processing->lighten = lighten;
    processing_update_curves(processing);
}

/* Have a guess what this does */
void processingSetExposureStops(processingObject_t * processing, double exposureStops)
{
    processing->exposure_stops = exposureStops;

    processingSetGamma(processing, processing->gamma_power);
    if( processing->gradient_enable != 0 ) processingSetGammaGradient(processing, processing->gamma_power);
    processing_update_matrices(processing);
    if( processing->gradient_enable != 0 ) processing_update_matrices_gradient(processing);
}

/* Have a guess what this does */
void processingSetGradientExposure(processingObject_t * processing, double value)
{
    processing->gradient_exposure_stops = value;

    processingSetGamma(processing, processing->gamma_power);
    processingSetGammaGradient(processing, processing->gamma_power);
    processing_update_matrices(processing);
    processing_update_matrices_gradient(processing);
}

/* Sets and precalculaes saturation */
void processingSetSaturation(processingObject_t * processing, double saturationFactor)
{
    processing->saturation = saturationFactor;

    /* Precaluclate for the algorithm */
    for (int i = 0; i < 131072; ++i)
    {
        double value = (i - 65536) * saturationFactor;
        processing->pre_calc_sat[i] = value;
    }
}


/* Sets and precalculaes vibrance */
void processingSetVibrance(processingObject_t *processing, double vibranceFactor)
{
    processing->vibrance = vibranceFactor;

    /* Precaluclate for the algorithm */
    for (int i = 0; i < 131072; ++i)
    {
        double value = (i - 65536) * vibranceFactor;
        processing->pre_calc_vibrance[i] = value;
    }
}


/* Set direction bias */
void processingSetSharpeningBias(processingObject_t * processing, double bias)
{
    processing->sharpen_bias = bias;
    /* Recalculates everythin */
    processingSetSharpening(processing, processingGetSharpening(processing));
}


void processingSetSharpening(processingObject_t * processing, double sharpen)
{
    processing->sharpen = sharpen;

    /* Anything more than ~0.5 just looks awful */
    sharpen = pow(sharpen, 1.5) * 0.55;

    double sharpen_x = sharpen * (1.0 - processing->sharpen_bias);
    double sharpen_y = sharpen * (1.0 + processing->sharpen_bias);
    double sharpen_a = 1.0 + (2.0 * sharpen_x) + (2.0 * sharpen_y);

    for (int i = 0; i < 65536; ++i)
    {
        processing->pre_calc_sharp_a[i] = (uint32_t)((double)i * sharpen_a);
        processing->pre_calc_sharp_x[i] = (uint16_t)LIMIT16((double)i * sharpen_x);
        processing->pre_calc_sharp_y[i] = (uint16_t)LIMIT16((double)i * sharpen_y);
    }
}

/* Set white balance by kelvin + tint value */
void processingSetWhiteBalance(processingObject_t * processing, double WBKelvin, double WBTint)
{
    double * p_xyz_to_rgb;
    double * p_ciecam02;

    if( processing->use_cam_matrix == 2 ) {
        /* Danne matrix fix converts to "sRGB", so we will convert back to "XYZ",
         * pretending Danne fix is real sRGB, and THEN convert to whatever RGB
         * space we actually want. TODO: remove this as soon as possible */
        p_xyz_to_rgb = alloca(9 * sizeof(double)); // I LOVE ALLOCA OMG
        double sRGB_to_xyz[9];
        invertMatrix(xyz_to_rgb, sRGB_to_xyz); // the xyz_to_rgb is for sRGB
        double DanneEffectMatrix[9]; /* Applies the Danne effect to an image in XYZ space */
        multiplyMatrices(sRGB_to_xyz, xyz_to_rgb_danne, DanneEffectMatrix);
        multiplyMatrices(colour_gamuts[processing->colour_gamut], DanneEffectMatrix, p_xyz_to_rgb);
        p_ciecam02 = ciecam02_danne;
    }
    else {
        //scientific camera matrix
        p_xyz_to_rgb = colour_gamuts[processing->colour_gamut]/* alloca(9 * sizeof(double)) */;
        p_ciecam02 = ciecam02;
    }

    processing->kelvin = WBKelvin;

    /* Avoid changing tint if just changing kelvin */
    if (WBTint != processing->wb_tint)
    {
        /* Non-linear tint makes control finer in the middle */
        int is_negative = (WBTint < 0.0);
        if (is_negative) WBTint = -WBTint;
        WBTint /= 10.0;
        WBTint = pow(WBTint, 1.75) * 10.0;
        if (is_negative) WBTint = -WBTint;

        processing->wb_tint = WBTint;
    }
    
    /* Calculate multipliers */

    /* double XYZ_White[3], XYZ_Temp[3], I[3] = {1,1,1};
    Kelvin_Daylight_to_XYZ(6500, XYZ_White);
    Kelvin_Daylight_to_XYZ(WBKelvin, XYZ_Temp);

    applyMatrix(XYZ_White, processing->cam_matrix);
    applyMatrix(XYZ_Temp, processing->cam_matrix);
    applyMatrix(I, processing->cam_matrix);

    for (int i = 0; i < 3; ++i)
    {
        processing->wb_multipliers[i] = XYZ_White[i]/(XYZ_Temp[i]*I[i]);
    } */

    get_kelvin_multipliers_rgb(WBKelvin, processing->wb_multipliers);

    /* Do tint (green and red channel seem to be main ones) */
    processing->wb_multipliers[2] += (WBTint / 11.0);
    processing->wb_multipliers[0] += (WBTint / 19.0);

    /* Make all channel multipliers be >= 1 */
    double lowest = MIN( MIN( processing->wb_multipliers[0], 
                              processing->wb_multipliers[1] ), 
                              processing->wb_multipliers[2] );

    for (int i = 0; i < 3; ++i) processing->wb_multipliers[i] /= lowest;

    /* White balance is part of the matrix */
    processing_update_matrices(processing);
    if( processing->gradient_enable != 0 ) processing_update_matrices_gradient(processing);


    /****************************** Now generate the matrix for scientific White Balance ******************************/

    double proper_wb_matrix[9] = {1,0,0,0,1,0,0,0,1};

    /* Get multipliers for this to undo what has been done, it was only done to do highlihgt reconstrucytion now */
    double multiplierz[3];
    get_kelvin_multipliers_rgb(WBKelvin, multiplierz); 

    /* Now create a matrix, which will take us back to raw colour by undoing
     * basic wb (which was useful for highlight reconstruction, also where tint was done) */
    double undo_basic_wb_matrix[9] = {
        1.0/multiplierz[0], 0, 0,
        0, 1.0/multiplierz[1], 0,
        0, 0, 1.0/multiplierz[2]
    };

    /* Get white points and convert to LMS space */
    double LMS_white[3];
    double LMS_temp[3];
    Kelvin_Daylight_to_XYZ(6500, LMS_white);
    Kelvin_Daylight_to_XYZ(WBKelvin, LMS_temp);
    applyMatrix(LMS_white, p_ciecam02);
    applyMatrix(LMS_temp, p_ciecam02);

    double LMS_multipliers[3];
    for (int i = 0; i < 3; ++i) LMS_multipliers[i] = LMS_white[i]/LMS_temp[i];

    double cam_to_xyz_D[9]; /* For daylight */
    double cam_to_xyz_A[9]; /* For tungsten (https://en.wikipedia.org/wiki/Standard_illuminant#Illuminant_A) */
    invertMatrix(processing->cam_matrix, cam_to_xyz_D);
    invertMatrix(processing->cam_matrix_A, cam_to_xyz_A);

    double cam_to_xyz_final[9];

    /* Blend the matrices between 3000 and 3600 Kelvin */
    int mixfac = (WBKelvin-3000) / 600.0;
    mixfac = MAX(MIN(1.0, mixfac), 0.0);
    for (int i = 0; i < 9; ++i)
    {
        cam_to_xyz_final[i] = cam_to_xyz_A[i]*(1.0-mixfac) + cam_to_xyz_D[i]*mixfac;
    }

    multiplyMatrices(cam_to_xyz_final, undo_basic_wb_matrix, proper_wb_matrix);

    /* Convert to LMS space */
    double matrix_in_LMS[9];
    multiplyMatrices(p_ciecam02, proper_wb_matrix, matrix_in_LMS);

    /* Apply multipliers in XYZ */
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            matrix_in_LMS[i*3+j] = matrix_in_LMS[i*3+j] * LMS_multipliers[i];
        }
    }

    /* Matrix back to XYZ from LMS */
    double LMS_to_XYZ[9];
    invertMatrix(p_ciecam02, LMS_to_XYZ);

    double back_in_XYZ_matrix[9];
    multiplyMatrices(LMS_to_XYZ, matrix_in_LMS, back_in_XYZ_matrix);

    /* Back to sRGB (maybe something wider in future) */
    multiplyMatrices(p_xyz_to_rgb, back_in_XYZ_matrix, processing->proper_wb_matrix);
}

/* WB just by kelvin */
void processingSetWhiteBalanceKelvin(processingObject_t * processing, double WBKelvin)
{
    processingSetWhiteBalance( processing, WBKelvin,
                               processingGetWhiteBalanceTint(processing) );
}

/* WB tint */
void processingSetWhiteBalanceTint(processingObject_t * processing, double WBTint)
{
    processingSetWhiteBalance( processing, processingGetWhiteBalanceKelvin(processing),
                               WBTint );
}

/* Set gamma (Log-ing / tonemapping done here) */
void processingSetGamma(processingObject_t * processing, double gammaValue)
{
    processing->gamma_power = gammaValue;

    /* Needs to be inverse */
    double gamma = 1.0 / gammaValue;

    double (* tone_mapping_function)(double) = tonemap_functions[processing->tonemap_function];

    if (processing->exposure_stops < 0.0 || processing->tonemap_function == 0)
    {
        /* Precalculate the exposure curve */
        for (int i = 0; i < 65536; ++i)
        {
            double pixel = (double)i/65535.0;
            pixel = tone_mapping_function(pixel);
            processing->pre_calc_gamma[i] = (uint16_t)(65535.0 * pow(pixel, gamma));
        }
    }
    /* Else exposure done here if it is positive */
    else
    {
        /* Exposure */
        double exposure_factor = pow(2.0, processing->exposure_stops);
        /* Precalculate the curve */
        for (int i = 0; i < 65536; ++i)
        {
            /* Tone mapping also (reinhard) */
            double pixel = (double)i/65535.0;
            pixel *= exposure_factor;
            pixel = tone_mapping_function(pixel);
            pixel = 65535.0 * pow(pixel, gamma);
            pixel = LIMIT16(pixel);
            processing->pre_calc_gamma[i] = pixel;
        }
    }
    /* So highlight reconstruction works */
    processing_update_highest_green(processing);
}

/* Set gamma for gradient image part (Log-ing / tonemapping done here) */
void processingSetGammaGradient(processingObject_t * processing, double gammaValue)
{
    processing->gamma_power = gammaValue;

    /* Needs to be inverse */
    double gamma = 1.0 / gammaValue;

    double (* tone_mapping_function)(double) = tonemap_functions[processing->tonemap_function];

    if (processing->exposure_stops+processing->gradient_exposure_stops < 0.0 || processing->tonemap_function == 0)
    {
        /* Precalculate the exposure curve */
        for (int i = 0; i < 65536; ++i)
        {
            double pixel = (double)i/65535.0;
            pixel = tone_mapping_function(pixel);
            processing->pre_calc_gamma_gradient[i] = (uint16_t)(65535.0 * pow(pixel, gamma));
        }
    }
    /* Else exposure done here if it is positive */
    else
    {
        /* Exposure */
        double exposure_factor = pow(2.0, processing->exposure_stops+processing->gradient_exposure_stops);
        /* Precalculate the curve */
        for (int i = 0; i < 65536; ++i)
        {
            /* Tone mapping also (reinhard) */
            double pixel = (double)i/65535.0;
            pixel *= exposure_factor;
            pixel = tone_mapping_function(pixel);
            pixel = 65535.0 * pow(pixel, gamma);
            pixel = LIMIT16(pixel);
            processing->pre_calc_gamma_gradient[i] = pixel;
        }
    }
    /* So highlight reconstruction works */
    processing_update_highest_green_gradient(processing);
}

/* Range of saturation and hue is 0.0-1.0 */
void processingSet3WayCorrection( processingObject_t * processing,
                                  double highlightHue, double highlightSaturation,
                                  double midtoneHue, double midtoneSaturation,
                                  double shadowHue, double shadowSaturation )
{
    processing->highlight_hue = highlightHue;
    processing->highlight_sat = highlightSaturation;
    processing->midtone_hue = midtoneHue;
    processing->midtone_sat = midtoneSaturation;
    processing->shadow_hue = shadowHue;
    processing->shadow_sat = shadowSaturation;

    processing_update_curves(processing);
}

// void processing_enable_tonemapping(processingObject_t * processing)
// {
//     (processing)->tone_mapping = 1;
//     /* This will update everything necessary to enable tonemapping */
//     processingSetGamma(processing, processing->gamma_power);
//     processingSetGammaGradient(processing, processing->gamma_power);
//     processing_update_matrices(processing);
//     processing_update_matrices_gradient(processing);
// }

// void processing_disable_tonemapping(processingObject_t * processing) 
// {
//     (processing)->tone_mapping = 0;
//     /* This will update everything necessary to disable tonemapping */
//     processingSetGamma(processing, processing->gamma_power);
//     processingSetGammaGradient(processing, processing->gamma_power);
//     processing_update_matrices(processing);
//     processing_update_matrices_gradient(processing);
// }

/* Set black and white level */
void processingSetBlackAndWhiteLevel( processingObject_t * processing, 
                                      int mlvBlackLevel, int mlvWhiteLevel, int mlvBitDepth )
{
    /* Convert levels to 16bit */
    int bits_shift = 16 - mlvBitDepth;
    if( mlvBlackLevel >= 0 )
    {
        if(mlvBlackLevel) processing->black_level = mlvBlackLevel << bits_shift;
        else processing->black_level = 0;
    }
    if( mlvWhiteLevel >= 0 )
    {
        processing->white_level = mlvWhiteLevel << bits_shift;
        /* Lowering white level a bit avoids pink grain in highlihgt reconstruction */
        processing->white_level = (int)((double)(mlvWhiteLevel << bits_shift) * 0.993);
    }

    /* How much it needs to be stretched */
    double stretch = 65535.0 / (double)(processing->white_level - processing->black_level);

    for (int i = 0; i < 65536; ++i)
    {
        /* Stretch to the black-white level range */
        int new_value = (int)((double)(i - processing->black_level) * stretch);

        if (new_value < 65536 && new_value > 0)
        {
            processing->pre_calc_levels[i] = new_value;
        }
        else if (new_value <= 0)
        {
            processing->pre_calc_levels[i] = 0;
        }
        else if (new_value >= 65535)
        {
            processing->pre_calc_levels[i] = 65535;
        }
    }
}

/* Cheat functions */
void processingSetBlackLevel(processingObject_t * processing, int mlvBlackLevel, int mlvBitDepth)
{
    processingSetBlackAndWhiteLevel( processing,
                                     mlvBlackLevel,
                                     -1, // if -1 leave value untouched
                                     mlvBitDepth );
}
void processingSetWhiteLevel(processingObject_t * processing, int mlvWhiteLevel, int mlvBitDepth)
{
    processingSetBlackAndWhiteLevel( processing,
                                     -1, // if -1 leave value untouched
                                     mlvWhiteLevel,
                                     mlvBitDepth );
}

/* Set transformation */
void processingSetTransformation(processingObject_t * processing, int transformation)
{
    processing->transformation = transformation;
}

/* Decomissions a processing object completely(I hope) */
void freeProcessingObject(processingObject_t * processing)
{
    if(processing->gradient_mask) free(processing->gradient_mask);
    if(processing->vignette_mask) free(processing->vignette_mask);
    freeFilterObject(processing->filter);
    free_lut(processing->lut);
    for (int i = 8; i >= 0; --i) free(processing->pre_calc_matrix[i]);
    for (int i = 8; i >= 0; --i) free(processing->pre_calc_matrix_gradient[i]);
    for (int i = 6; i >= 0; --i) free(processing->cs_zone.pre_calc_rgb_to_YCbCr[i]);
    for (int i = 3; i >= 0; --i) free(processing->cs_zone.pre_calc_YCbCr_to_rgb[i]);
    free_image_buffer(processing->shadows_highlights.blur_image);
    free(processing);
}

/* Find correct white balance setting for one selected pixel */
void processingFindWhiteBalance(processingObject_t *processing, int imageX, int imageY, uint16_t *inputImage, int posX, int posY, int *wbTemp, int *wbTint, int mode)
{
    /* Number of elements */
    int img_s = imageX * imageY * 3;

    /* (for shorter code) */
    int32_t ** pm = processing->pre_calc_matrix;
    uint16_t * img = inputImage;

    /* Apply some precalcuolated settings */
    for (int i = 0; i < img_s; ++i)
    {
        /* Black + white level */
        img[i] = processing->pre_calc_levels[ img[i] ];
    }

    double oriTemp = processing->kelvin;
    double oriTint = processing->wb_tint;
    int nearestTemp;
    int nearestTint;
    uint32_t deltaMin = UINT32_MAX;

    /* this is the pixel for what we search the parameters */
    uint32_t pixR = 0;
    uint32_t pixG = 0;
    uint32_t pixB = 0;
    uint32_t counter = 0;

    /* average with neighbor pixels */
    for( int y = -10; y <= 10; y++ )
    {
        for( int x = -10; x <= 10; x++ )
        {
            if( posX+x < 0 || posY+y < 0 || posX+x >= imageX || posY+y >= imageY ) continue;

            uint16_t * pix = img + ( ( (posY+y) * imageX + (posX+x) ) * 3 );

            pixR += pix[0];
            pixG += pix[1];
            pixB += pix[2];
            counter++;
        }
    }
    pixR /= counter;
    pixG /= counter;
    pixB /= counter;

    /* skin wb mode */
    if( mode == 1 )
    {
        pixR *= 1.0 / (4686.0 / 6069 * 1.979);
        pixB *= 1.0 / (2817.0 / 6069 * 1.678);
    }

    /* activate quick matrix build (we need matrix just for this RGB values) */
    processing->wbR = pixR;
    processing->wbG = pixG;
    processing->wbB = pixB;
    processing->wbFindActive = 1;

    /* Trail & Error :-P */
    for( int temp = 2300; temp <= 10000; temp += 10 )
    {
        for( int tint = -100; tint <= 100; tint += 1 )
        {
            processingSetWhiteBalance( processing, temp, tint/10.0 );

            /* --- maybe this can also be exchanged by apply_processing_object, but here it is simplified and hopefully faster --- */
            /* white balance & exposure */
            int32_t pix0 = LIMIT16(pm[0][pixR] /*+ pm[1][pixG] + pm[2][pixB]*/);
            int32_t pix1 = LIMIT16(/*pm[3][pixR] +*/ pm[4][pixG] /*+ pm[5][pixB]*/);
            int32_t pix2 = LIMIT16(/*pm[6][pixR] + pm[7][pixG] +*/ pm[8][pixB]);

            /* standard highlight reconstruction */
            if( processing->highlight_reconstruction && pix1 == processing->highest_green )
            {
                pix1 = ( pix0 + pix2 ) / 2;
            }
            /* --- */

            if( processing->use_cam_matrix > 0 )
            {
                uint16_t pix0b = pix0, pix1b = pix1, pix2b = pix2;
                double result[3];
                result[0] = pix0b * processing->proper_wb_matrix[0] + pix1b * processing->proper_wb_matrix[1] + pix2b * processing->proper_wb_matrix[2];
                result[1] = pix0b * processing->proper_wb_matrix[3] + pix1b * processing->proper_wb_matrix[4] + pix2b * processing->proper_wb_matrix[5];
                result[2] = pix0b * processing->proper_wb_matrix[6] + pix1b * processing->proper_wb_matrix[7] + pix2b * processing->proper_wb_matrix[8];
                pix0 = LIMIT16(result[0]);
                pix1 = LIMIT16(result[1]);
                pix2 = LIMIT16(result[2]);
            }
            pix0 = processing->pre_calc_gamma[ pix0 ];
            pix1 = processing->pre_calc_gamma[ pix1 ];
            pix2 = processing->pre_calc_gamma[ pix2 ];

            /* for neutral grey all 3 are the same, so searching the min delta */
            uint32_t delta = abs( pix0 - pix1 ) + abs( pix1 - pix2 ) + abs( pix0 - pix2 );

            if( delta < deltaMin )
            {
                nearestTemp = temp;
                nearestTint = tint;
                deltaMin = delta;
            }

            /* delta won't be smaller than 0 */
            if( deltaMin == 0 ) break;
        }

        /* delta won't be smaller than 0 */
        if( deltaMin == 0 ) break;
    }

    /* deactivate quick matrix build */
    processing->wbFindActive = 0;

    /* set it back to where we began */
    processingSetWhiteBalance( processing, (double)oriTemp, (double)oriTint );

    /* give the GUI what it wanted */
    *wbTemp = nearestTemp;
    *wbTint = nearestTint;
}

/* Vignette Mask Creation */
void processingSetVignetteMask(processingObject_t *processing, uint16_t width, uint16_t height, float radius, float shape, float xStretch, float yStretch)
{
    double wHalf = width / 2.0;
    double hHalf = height / 2.0;
    double wHalfS = wHalf * xStretch;
    double hHalfS = hHalf * yStretch * (1.0 + shape * 2.0);
    double diagonal = sqrt( (wHalfS*wHalfS) + (hHalfS*hHalfS) );
    double r = diagonal * radius * ( 1.0 + shape / 5.0 );
    double T = diagonal - r;
    double cosTerm = 2.0*M_PI/T/4.0;

    processing->vignette_end = processing->vignette_mask + (width*height);

    //#pragma omp parallel for collapse(2)
    for( uint16_t x = 0; x < (uint16_t)wHalf; x++ )
    {
        #pragma omp parallel for
        for( uint16_t y = 0; y < (uint16_t)hHalf; y++ )
        {
            double w = fabs( (double)x * xStretch - wHalfS );
            double h = fabs( (double)y * yStretch - hHalfS );
            double d = sqrt( (w*w) + (h*h) );
            float val = 0.0;
            //only behind radius
            if( d > r )
            {
                //cos curve
                val = 0.5-0.5*cos(cosTerm*(d-r));
            }
            //4x faster...
            processing->vignette_mask[y*width+x] = val;
            processing->vignette_mask[(height-1-y)*width+x] = val;
            processing->vignette_mask[y*width+(width-1-x)] = val;
            processing->vignette_mask[(height-1-y)*width+(width-1-x)] = val;
        }
    }
}

void processingSetVignetteStrength(processingObject_t *processing, int8_t value)
{
    processing->vignette_strength = value;
}

/* Set and calculate the gradient alpha mask */
void processingSetGradientMask(processingObject_t *processing, uint16_t width, uint16_t height, float x1, float y1, float x2, float y2)
{
    float A = (x2 - x1);
    float B = (y2 - y1);
    float C1 = A * x1 + B * y1;
    float C2 = A * x2 + B * y2;

    for( uint16_t x = 0; x < width; x++ )
    {
        #pragma omp parallel for
        for( uint16_t y = 0; y < height; y++ )
        {
            float C = A * x + B * y;
            if( C <= C1 ) processing->gradient_mask[y*width+x] = 0;
            else if( C >= C2 ) processing->gradient_mask[y*width+x] = 65535;
            else
            {
                processing->gradient_mask[y*width+x] = LIMIT16( ( 65535 * ( C - C1 ) ) / ( C2 - C1 ) );
            }
        }
    }
}

/* Analyse dual iso frame to find highest green for highlight reconstruction */
void analyse_frame_highest_green(processingObject_t *processing, int imageX, int imageY, uint16_t *inputImage)
{
    //if not dual iso, we don't need to do this
    if ( *processing->dual_iso == 0 ) return;

    uint16_t * img = inputImage;
    int img_s = imageX * imageY * 3;
    uint16_t * img_end = img + img_s;
    int32_t ** pm = processing->pre_calc_matrix;
    int32_t ** pmg = processing->pre_calc_matrix_gradient;

    //printf( "start algo. \r\n" );

    //if ( processing->highlight_reconstruction )
    {
        /* for dual iso the highest green peak has to be searched */
        /* build histogram for green channel */
        uint16_t tableG[256] = {0};
        for (uint16_t * pix = img; pix < img_end; pix += 3)
        {
            uint16_t pix1 = LIMIT16( pm[4][processing->pre_calc_levels[pix[1]]] )>>8;
            if( pix1 > 255 ) pix1 = 255;
            tableG[pix1]++;
        }
        /* search the brightest (the most right) peak (I made it equivalent to the number of lines to process in the image or more) */
        int prevVal = 0;
        uint8_t dir = 0;
        uint8_t cnt = 0;
        int lastPeak = 1;
        //PARAMETERS
        int abrtDelt = 5000;
        int abrtPeak = imageX * imageY / 400;
        int abrtSign = imageX * imageY / 4000;
        // /////////
        for( int32_t i = 255; i > 2; i-- ) //only down to 5, below we just have dark noise, but no highlight
        {
            int curVal = tableG[i];
            if (prevVal < curVal) {  // (still) ascending?
                dir = 0;
            }
            else if (prevVal > curVal) { // (still) descending?
                if (dir != 1) { // starts descending?
                    int delta = prevVal-lastPeak;
                    //printf( "peak at index %d %d %d %d %d \r\n", (i-1)<<8, prevVal, cnt, abrtPeak, delta );
                    //This should be the highlight
                    if( prevVal > abrtPeak && delta > abrtDelt )
                    {
                        processing->highest_green_diso = (i-1)<<8;
                        break;
                    }
                    lastPeak = prevVal;
                    dir = 1;
                    //This should be already normal picture data... stop, there is no clipped highlight
                    if( prevVal > abrtSign )
                    {
                        processing->highest_green_diso = 65535;
                        break;
                    }
                    cnt++;
                }
            }
            // prevVal == curVal is simply ignored...
            prevVal = curVal;
        }

        /* And now the same for the gradient part image */
        if( processing->gradient_enable && ( ( processing->gradient_exposure_stops < -0.01 || processing->gradient_exposure_stops > 0.01 )
                                          || ( processing->gradient_contrast < -0.01 || processing->gradient_contrast > 0.01 ) ) )
        {
            uint16_t tableGg[256] = {0};
            for (uint16_t * pix = img; pix < img_end; pix += 3)
            {
                uint16_t pix1 = LIMIT16( pmg[4][processing->pre_calc_levels[pix[1]]] )>>8;
                if( pix1 > 255 ) pix1 = 255;
                tableGg[pix1]++;
            }
            /* search the brightest (the most right) peak (I made it equivalent to the number of lines to process in the image or more) */
            prevVal = 0;
            dir = 0;
            cnt = 0;
            for( int32_t i = 255; i > 2; i-- )
            {
                int curVal = tableGg[i];
                if (prevVal < curVal) {  // (still) ascending?
                    dir = 0;
                }
                else if (prevVal > curVal) { // (still) descending?
                    if (dir != 1) { // starts descending?
                        int delta = prevVal-lastPeak;
                        //This should be the highlight
                        if( prevVal > abrtPeak && delta > abrtDelt )
                        {
                            processing->highest_green_gradient_diso = (i-1)<<8;
                            break;
                        }
                        lastPeak = prevVal;
                        dir = 1;
                        //This should be already normal picture data... stop, there is no clipped highlight
                        if( prevVal > abrtSign )
                        {
                            processing->highest_green_gradient_diso = 65535;
                            break;
                        }
                        cnt++;
                    }
                }
                // prevVal == curVal is simply ignored...
                prevVal = curVal;
            }
        }
    }
}

//Set LUT strength factor
void processingSetLutStrength(processingObject_t *processing, uint8_t strength)
{
    processing->lut->intensity = strength;
}

//Set the gradation curve
void processingSetGCurve(processingObject_t *processing, int num, float *pXin, float *pYin, uint8_t channel)
{
    uint16_t *curve;
    if( channel == 1 ) curve = processing->gcurve_r;
    else if( channel == 2 ) curve = processing->gcurve_g;
    else if( channel == 3 ) curve = processing->gcurve_b;
    else curve = processing->gcurve_y;

    //Init
    if( num < 2 )
    {
        for( int i = 0; i < 65536; i++ )
        {
            curve[i] = i;
        }
        return;
    }

    //Build output sets
    float *pXout = (float*)malloc( sizeof(float) * 65536 );
    float *pYout = (float*)malloc( sizeof(float) * 65536 );

    int numOut = 65536;

    //Data into x of output sets
    for( int i = 0; i < numOut; i++ )
    {
        pXout[i] = i / (float)65536.0;
    }

    //Get the interpolated line
    int ret = spline1dc( pXin , pYin , &num,
                         pXout, pYout, &numOut );

    if( ret == 0 )
    {
        for( int i = 0; i < 65536; i++ )
        {
            if( pYout[i] > 1.0 ) pYout[i] = 1.0;
            else if( pYout[i] < 0.0001 ) pYout[i] = 0.0001;
            curve[i] = pYout[i] * 65535.0;
        }
    }

    free( pXout );
    free( pYout );
}

//Set the hue vs curves
void processingSetHueVsCurves(processingObject_t *processing, int num, float *pXin, float *pYin, uint8_t channel)
{
    float *curve;
    uint8_t *used;
    if( channel == 0 )
    {
        curve = processing->hue_vs_hue;
        used = &processing->hue_vs_hue_used;
    }
    else if( channel == 1 )
    {
        curve = processing->hue_vs_saturation;
        used = &processing->hue_vs_saturation_used;
    }
    else if( channel == 2 )
    {
        curve = processing->hue_vs_luma;
        used = &processing->hue_vs_luma_used;
    }
    else
    {
        curve = processing->luma_vs_saturation;
        used = &processing->luma_vs_saturation_used;
    }
    //Init
    if( num < 2 )
    {
        for( int i = 0; i < 36000; i++ )
        {
            curve[i] = 0.0;
            *used = 0;
        }
        return;
    }

    //Build output sets
    float *pXout = (float*)malloc( sizeof(float) * 36000 );
    float *pYout = (float*)malloc( sizeof(float) * 36000 );

    int numOut = 36000;

    //Data into x of output sets
    for( int i = 0; i < numOut; i++ )
    {
        pXout[i] = i / (float)36000.0;
    }

    //Get the interpolated line
    int ret = cosine_interpolate( pXin , pYin , &num,
                                  pXout, pYout, &numOut );

    if( ret == 0 )
    {
        *used = 0;
        for( int i = 0; i < 36000; i++ )
        {
            if( pYout[i] > 1.0 ) pYout[i] = 1.0;
            else if( pYout[i] < -1.0 ) pYout[i] = -1.0;
            curve[i] = pYout[i];
            if( curve[i] != 0.0 ) *used = 1;
        }
    }

    free( pXout );
    free( pYout );
}

/* Toning */
void processingSetToning(processingObject_t *processing, uint8_t r, uint8_t g, uint8_t b, uint8_t strength)
{
    processing->toning_dry = (100.0 - strength / 3.0) / 100.0;
    processing->toning_wet[0] = (strength / 3.0 / 100.0) * (float)r / 255.0;
    processing->toning_wet[1] = (strength / 3.0 / 100.0) * (float)g / 255.0;
    processing->toning_wet[2] = (strength / 3.0 / 100.0) * (float)b / 255.0;
}
