#ifndef _raw_processing_
#define _raw_processing_

#include "processing_object.h"
#include "filter/filter.h"


/* Intitialises a 'processing object' which is a structure 
 * that makes it easy to contol all the processing */
processingObject_t * initProcessingObject();
/* Opposite of the first fucntion */
void freeProcessingObject(processingObject_t * processing);



/* Set one of default image profiles */
void processingSetImageProfile(processingObject_t * processing, int imageProfile);
/* imageProfile argument options: */
#define PROFILE_STANDARD    0   /* Gamma Corrected */
#define PROFILE_TONEMAPPED  1   /* Gamma Corrected + Tonemapped */
#define PROFILE_FILM        2   /* Gamma Corrected + inverse tangent tonemap */
// #define PROFILE_CANON_LOG   2   /* Canon C-Log (commented out - not working) */
#define PROFILE_ALEXA_LOG   3   /* Alexa log (A form of Log-C) */
#define PROFILE_CINEON_LOG  4   /* Cineon Log */
#define PROFILE_SONY_LOG_3  5   /* Sony S-Log 3 */
#define PROFILE_LINEAR      6   /* Linear, idk who would want this */

/* Set a custom image profile using the image_profile struct */
void processingSetCustomImageProfile(processingObject_t * processing, image_profile_t * imageProfile);



/* Process a RAW frame with settings from a processing object
 * - image must be debayered and RGB plz + thx! */
void applyProcessingObject( processingObject_t * processing, 
                            int imageX, int imageY, 
                            uint16_t * __restrict inputImage, 
                            uint16_t * __restrict outputImage,
                            int threads, int imageChanged );



/* Enable/disable the filter module (filter/filter.h) */
#define processingEnableFilters(processing) processing->filter_on = 1
#define processingDisableFilters(processing) processing->filter_on = 0



/* Highlights/shadows, input: -1.0 to +1.0 (show as -100 to +100) */
void processingSetHighlights(processingObject_t * processing, double value);
void processingSetShadows(processingObject_t * processing, double value);



/* Set contrast(S-curve really) - important: precalculates values, 
 * so don't do it manually, we r OOPing so hard right now */
void processingSetContrast( processingObject_t * processing, 
                            double DCRange,  /* Dark contrast range: 0.0 to 1.0 */
                            double DCFactor, /* Dark contrast strength: 0.0 to 8.0(any range really) */
                            double LCRange,  /* Light contrast range */
                            double LCFactor, /* Light contrast strength */ 
                            double lighten   /* 0-1 (for good highlight rolloff) */ );
void processingSetDCRange(processingObject_t * processing, double DCRange);
void processingSetDCFactor(processingObject_t * processing, double DCFactor);
void processingSetLCRange(processingObject_t * processing, double LCRange);
void processingSetLCFactor(processingObject_t * processing, double LCFactor);
void processingSetLightening(processingObject_t * processing, double lighten); /* RANGE IS 0.0-0.6 */
/* For getting info about contrast */
#define processingGetDCRange(processing) (processing)->dark_contrast_range
#define processingGetDCFactor(processing) (processing)->dark_contrast_factor
#define processingGetLCRange(processing) (processing)->light_contrast_range
#define processingGetLCFactor(processing) (processing)->light_contrast_factor
#define processingGetLightening(processing) (processing)->lighten



/* Set sharpening, 0.0-1.0 range */
void processingSetSharpening(processingObject_t * processing, double sharpen);
#define processingGetSharpening(processing) (processing)->sharpen
/* Set direction bias... 0=equal, -1=horizontal, 1=vertical */
void processingSetSharpeningBias(processingObject_t * processing, double bias);
#define processingGetSharpeningBias(processing) (processing)->sharpen_bias



/* 3-way correction, range of saturation and hue is 0.0-1.0 (Currently not doing anything) */
void processingSet3WayCorrection( processingObject_t * processing,
                                  double highlightHue, double highlightSaturation,
                                  double midtoneHue, double midtoneSaturation,
                                  double shadowHue, double shadowSaturation );


/* Enable/disable highlight reconstruction */
#define processingEnableChromaSeparation(processing) (processing)->cs_zone.use_cs = 1
#define processingDisableChromaSeparation(processing) (processing)->cs_zone.use_cs = 0
#define processingUsesChromaSeparation(processing) (processing)->cs_zone.use_cs /* A checking function */


/* Chroma blur - to enable it, you MUST enable chroma separation too. */
#define processingSetChromaBlurRadius(processing, radius) (processing)->cs_zone.chroma_blur_radius = (radius)
#define processingGetChromaBlurRadius(processing) (processing)->cs_zone.chroma_blur_radius



/* Just don't touch this or keep at ~2.2 (or more for a lighter image) */
void processingSetGamma(processingObject_t * processing, double gammaValue);
#define processingGetGamma(processing) (processing)->gamma_power



/* Enable/disable highlight reconstruction */
#define processingEnableHighlightReconstruction(processing) (processing)->highlight_reconstruction = 1
#define processingDisableHighlightReconstruction(processing) (processing)->highlight_reconstruction = 0



/* Set Camera RAW -> sRGB matrix */
void processingCamTosRGBMatrix(processingObject_t * processing, double * camTosRGBMatrix);



/* Sets a processing object's Exposure: 0 = no adjustment */
void processingSetExposureStops(processingObject_t * processing, double exposureStops);
/* Gets a processing object's exposure value in stops */
#define processingGetExposureStops(processing) (processing)->exposure_stops



/* Set white balance by kelvin and/or tint value; Kelvin range: 2500-10000, tint -10 to +10 */
void processingSetWhiteBalance(processingObject_t * processing, double WBKelvin, double WBTint);
void processingSetWhiteBalanceKelvin(processingObject_t * processing, double WBKelvin);
void processingSetWhiteBalanceTint(processingObject_t * processing, double WBTint);
#define processingGetWhiteBalanceKelvin(processing) (processing)->kelvin
#define processingGetWhiteBalanceTint(processing) (processing)->wb_tint



/* Black/white level set */
void processingSetBlackLevel(processingObject_t * processing, int blackLevel);
void processingSetWhiteLevel(processingObject_t * processing, int whiteLevel);
void processingSetBlackAndWhiteLevel( processingObject_t * processing, 
                                      int blackLevel, int whiteLevel );
/* Get black/white level */
#define processingGetBlackLevel(processing) (processing)->black_level
#define processingGetWhiteLevel(processing) (processing)->white_level



/* Saturation setting: 1.0 = no saturation added, 0.0 = black and white ... */
void processingSetSaturation(processingObject_t * processing, double saturationFactor);
/* Get saturation - I don't see the use but maybe useful */
#define processingGetSaturation(processing) (processing)->saturation



/* Enable/disable tonemapping - DEPRECATED!!! (made private) */
#define processingEnableTonemapping(processing) processing_enable_tonemapping(processing)
#define processingDisableTonemapping(processing) processing_disable_tonemapping(processing)


/* Set transformation */
void processingSetTransformation(processingObject_t * processing, int transformation);


/*
 *******************************************************************************
 * THE FOLLOWING FUNCTIONS ARE PRIVATE AND NO USE OUTSIDE OF raw_processing.c
 ******************************************************************************
 */

/* Private function */
void apply_processing_object( processingObject_t * processing, 
                              int imageX, int imageY, 
                              uint16_t * __restrict inputImage, 
                              uint16_t * __restrict outputImage,
                              uint16_t * __restrict blurImage );

/* Pass frame buffer and do the transform on it */
void get_frame_transformed(processingObject_t * processing, uint16_t * frame_buf , uint16_t imageX, uint16_t imageY);

/* Useful info:
 * http://www.magiclantern.fm/forum/index.php?topic=19270
 * Thanks a1ex & g3gg0 */

typedef struct {
    processingObject_t * processing;
    int imageX, imageY;
    uint16_t * inputImage;
    uint16_t * outputImage;
    uint16_t * blurImage;
} apply_processing_parameters_t;

/* applyProcessingObject but with one argument for pthreading  */
void processing_object_thread(apply_processing_parameters_t * p);

/* Calculate shadow/hiughlight exposure LUT */
void processing_update_shadow_highlight_curve(processingObject_t * processing);

/* Adds contrast to a single pixel in a S-curvey way, 
 * input pixel must be 0.0 - 1.0 and a double float */
double add_contrast( double pixel,
                     double dark_contrast_range, 
                     double dark_contrast_factor, 
                     double light_contrast_range, 
                     double light_contrast_factor );

/* Enable/disable tonemapping */
void processing_enable_tonemapping(processingObject_t * processing);
void processing_disable_tonemapping(processingObject_t * processing);

/* Returns multipliers for white balance by (linearly) interpolating measured 
 * Canon values... stupidly simple, also range limited to 2500-10000 */
void get_kelvin_multipliers_rgb(double kelvin, double * multiplier_output);
void get_kelvin_multipliers_xyz(double T, double * RGB);

/* Calculates final_matrix, incorporating white balance, exposure and all the XYZ stuff */
void processing_update_matrices(processingObject_t * processing);

/* Calculates green clip value, so highlight reconstruction can work */
void processing_update_highest_green(processingObject_t * processing);

/* Precalculates curve with contrast and colour correction */
void processing_update_curves(processingObject_t * processing);

/* Pretty good function */
void hsv_to_rgb(double hue, double saturation, double value, double * rgb);

/* 3 way colour correction */
void colour_correct_3_way( double * rgb,
                           double h_hue, double h_sat,
                           double m_hue, double m_sat,
                           double s_hue, double s_sat );

/* Just a awful box blur right now */
void blur_image( uint16_t * __restrict in,
                 uint16_t * __restrict out,
                 int width, int height, int radius,
                 int do_r, int do_g, int do_b,
                 int start_y, int end_y );
/* JUst to be separate */
void convert_rgb_to_YCbCr(uint16_t * __restrict img, int32_t size, int32_t ** lut);
void convert_YCbCr_to_rgb(uint16_t * __restrict img, int32_t size, int32_t ** lut);

/* Tonemapping funtions */

/* Uncharted and Reinhard: http://filmicworlds.com/blog/filmic-tonemapping-operators/ */
double uncharted_tonemap(double x);
double UnchartedTonemap(double x);
double ReinhardTonemap(double x);
float ReinhardTonemap_f(float x);
/* Inverse tangent based tonemap - interesting... filmmic? */
double TangentTonemap(double x);
float TangentTonemap_f(float x);
/* Canon C-Log: http://learn.usa.canon.com/app/pdfs/white_papers/White_Paper_Clog_optoelectronic.pdf */
double CanonCLogTonemap(double x); /* Not working right */
float CanonCLogTonemap_f(float x);
/* Calculate Alexa Log curve (iso 800 version), from here: http://www.vocas.nl/webfm_send/964 */
double AlexaLogCTonemap(double x);
float AlexaLogCTonemap_f(float x);
/* Cineon Log, formula from here: http://www.magiclantern.fm/forum/index.php?topic=15801.msg158145#msg158145 */
double CineonLogTonemap(double x);
float CineonLogTonemap_f(float x);
/* Sony S-Log3, from here: https://www.sony.de/pro/support/attachment/1237494271390/1237494271406/technical-summary-for-s-gamut3-cine-s-log3-and-s-gamut3-s-log3.pdf */
double SonySLogTonemap(double x);
float SonySLogTonemap_f(float x);

/* Little image buffer 'class' for storing stuff that takes long to compute,
 * like blur (unused so far) */

#endif
