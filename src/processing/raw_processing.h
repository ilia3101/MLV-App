#ifndef _raw_processing_
#define _raw_processing_

/* NOTE: highlights and shadows must be done at the start */

#include "processing_object.h"

/* Intitialises a 'processing object' which is a structure 
 * that makes it easy to contol all the processing */
processingObject_t * initProcessingObject();
/* Opposite of the first fucntion */
void freeProcessingObject(processingObject_t * processing);



/* Set image profile possible TODO: make image profile a type, so custom ones can be developed */
void processingSetImageProfile(processingObject_t * processing, int imageProfile);
/* imageProfile argument options: */
#define PROFILE_STANDARD    0   /* Gamma Corrected */
#define PROFILE_TONEMAPPED  1   /* Gamma Corrected + Tonemapped */
// #define PROFILE_CANON_LOG   2   /* Canon C-Log (commented out - not working) */
#define PROFILE_ALEXA_LOG   2   /* Alexa log (A form of Log-C) */
#define PROFILE_CINEON_LOG  3   /* Cineon Log */
#define PROFILE_SONY_LOG_3  4   /* Sony S-Log 3 */
#define PROFILE_LINEAR      5   /* Linear, idk who would want this */



/* Process a RAW frame with settings from a processing object
 * - image must be debayered and RGB plz + thx! */
void applyProcessingObject( processingObject_t * processing,
                            int imageX, int imageY,
                            uint16_t * __restrict inputImage,
                            uint16_t * __restrict outputImage );



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



/* 3-way correction, range of saturation and hue is 0.0-1.0 (Currently not doing anything) */
void processingSet3WayCorrection( processingObject_t * processing,
                                  double highlightHue, double highlightSaturation,
                                  double midtoneHue, double midtoneSaturation,
                                  double shadowHue, double shadowSaturation );



/* Just don't touch this or keep at ~2.2 (or more for a lighter image) */
void processingSetGamma(processingObject_t * processing, double gammaValue);
#define processingGetGamma(processing) processing->gamma_power



/* Enable/disable highlight reconstruction */
#define processingEnableHighlightReconstruction(processing) (processing)->highlight_reconstruction = 1
#define processingDisableHighlightReconstruction(processing) (processing)->highlight_reconstruction = 0



/* Those ML matrices, camera specific */
void processingSetXyzToCamMatrix(processingObject_t * processing, double * xyzToCamMatrix);
/* This is whatever, probably doesn't need touching */
void processingSetXyzToRgbMatrix(processingObject_t * processing, double * xyzToRgbMatrix);



/* Sets a processing object's Exposure: 0 = no adjustment */
void processingSetExposureStops(processingObject_t * processing, double exposureStops);
/* Gets a processing object's exposure value in stops */
#define processingGetExposureStops(processing) (processing)->exposure_stops



/* Set white balance by kelvin and/or tint value; Kelvin range: 2500-10000, tint -inf to +inf */
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



/*
 *******************************************************************************
 * THE FOLLOWING FUNCTIONS ARE PRIVATE AND NO USE OUTSIDE OF raw_processing.c
 ******************************************************************************
 */


/* Useful info:
 * http://www.magiclantern.fm/forum/index.php?topic=19270
 * Thanks a1ex & g3gg0 */

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

/* Precalculates curve with contrast and colour correction */
void processing_update_curves(processingObject_t * processing);

/* Pretty good function */
void hsv_to_rgb(double hue, double saturation, double value, double * rgb);

/* 3 way colour correction */
void colour_correct_3_way( double * rgb,
                           double h_hue, double h_sat,
                           double m_hue, double m_sat,
                           double s_hue, double s_sat );

#endif
