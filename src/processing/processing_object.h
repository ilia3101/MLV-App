#ifndef _processing_struct_
#define _processing_struct_

#include "image_profile.h"
#include "filter/filter.h"
#include "cube_lut.h"

enum transform { TR_NONE, TR_ROT180 };

typedef struct {
    uint16_t width, height;
    uint16_t * image;
} processing_buffer_t;

/* Processing settings structure (a mess) */
typedef struct {

    filterObject_t * filter;
    int filter_on;

    lut_t * lut;
    int lut_on;

    /* If whitebalance find algorithm is on the run, we need it only for one single RGB -> faster */
    int wbFindActive;
    uint16_t wbR, wbG, wbB;

    /* Image profile, options:
     * PROFILE_STANDARD   : Gamma Corrected
     * PROFILE_TONEMAPPED : Gamma Corrected + Tonemapped
     * PROFILE_ALEXA_LOG  : Alexa log (A form of Log-C)
     * PROFILE_CINEON_LOG : Cineon Log
     * PROFILE_SONY_LOG_3 : Sony S-Log 3
     * PROFILE_LINEAR     : Linear, idk who would want this */
    image_profile_t * image_profile; /* Affects following two parameters */

    /* (RAW) white and black levels */
    int black_level, white_level;

    /* Do highlight reconstrucion? It's slow */
    int highlight_reconstruction;
    uint16_t highest_green; /* Used for reconstruction */
    uint16_t highest_green_gradient; /* Used for reconstruction */
    uint16_t highest_green_diso; /* Used for reconstruction */
    uint16_t highest_green_gradient_diso; /* Used for reconstruction */

    /* Gradation Curves */
    uint16_t gcurve_y[65536];
    uint16_t gcurve_r[65536];
    uint16_t gcurve_g[65536];
    uint16_t gcurve_b[65536];

    /* HSL parameters */
    float hue_vs_hue[36000];
    float hue_vs_saturation[36000];
    float hue_vs_luma[36000];
    float luma_vs_saturation[36000];
    uint8_t hue_vs_hue_used;
    uint8_t hue_vs_saturation_used;
    uint8_t hue_vs_luma_used;
    uint8_t luma_vs_saturation_used;

    /* toning */
    float toning_dry;
    float toning_wet[3];

    /* Camera's matrix - will need to be set on opening clip, default set for 5D Mark II */
    double cam_matrix[9];
    double cam_matrix_A[9]; /* A matrix better for temperature ~2856k (illuminant A) */

    /* Where the proper WB matrix goes */
    double proper_wb_matrix[9];

    /* Main matrix: combined white balance + exposure + whatever the cmaera matrix does */
    double final_matrix[9];

    /* Precalculated all matrix values 0-65535 */
    int32_t * pre_calc_matrix[9];
    int32_t * pre_calc_matrix_gradient[9];

    struct {
        /* "use chroma separation" */
        int use_cs;

        /* Look up tables for gettng in'n'out of YCbCr */
        int32_t * pre_calc_rgb_to_YCbCr[7];
        int32_t * pre_calc_YCbCr_to_rgb[5];

        /* Moire/noise filter (only avalible if use_cs is true) */
        uint32_t chroma_blur_radius;

        /* Cached blur image for faster single frame processing */
        // processing_buffer_t * blur_image;
    } cs_zone;

    /* All shadow/highlight stuff goes here */
    struct {
        /* Shadow-highlight values, -1.0 to 1.0 */
        double highlights;
        double shadows;

        /* Highlights/shadows precalculated exposure factors (not end result, that would take 8gb) */
        double shadow_highlight_curve[65536];

        /* Blurred image for highlights/shadows */
        processing_buffer_t * blur_image;
    } shadows_highlights;

    /* White balance */
    double     kelvin; /* from 2500 to 10000 */
    double     wb_tint; /* from -10 to +10 PLEAZ */

    /* Generic processing things */
    double     exposure_stops; /* Make this -4 to +4 STOPS */
    double     saturation; /* Slider from 0 to 2, to power of log(base2)of 3.5, so 1 stays in the middle, but max is 3.5 */
    double     vibrance; /* Slider from 0 to 2, to power of log(base2)of 3.5, so 1 stays in the middle, but max is 3.5 */
    double     contrast; /* Slider from -100 to 100 */
    double     contrast_curve[65536]; /* Contrast precalculated exposure factors */

    /* Clarity */
    double     clarity;
    double     clarity_curve[65536]; /* Curve for clarity */

    /* The two part 'contrast' or S-curve */
    double     light_contrast_factor; /* 0 - 5 */
    double     light_contrast_range; /* 0 - 1, or how much of the value range will be affected */
    double     dark_contrast_factor; /* 0 - 5 */
    double     dark_contrast_range; /* 0 - 1, or how much of the value range will be affected */

    /* 3 way colour correction */
    /* Hue is 0.0 - 1.0 */
    double     highlight_hue;
    double     midtone_hue;
    double     shadow_hue;
    /* Saturation also 0.0 - 1.0 */
    double     highlight_sat;
    double     midtone_sat;
    double     shadow_sat;

    /* Gamma power, applied after "tonemapping" function */
    double     gamma_power;
    /* Limited to 0.0 - 0.6 range */
    double     lighten;

    /* Sharpen: 0.0-1.0 range; default 0 */
    double     sharpen;
    double     sharpen_bias; /* 0=equal, -1=horizontal, 1=vertical */
    uint8_t    sh_masking; /*0..100, no mask .. full mask*/

    /* For whitebalance */
    double     wb_multipliers[3];
    
    /* These are pre-calculated curves / values for contrast and stuff 
     * will be calculated on setting changes, values 0-65535 */
    uint16_t   pre_calc_curve_r[65536];
    uint16_t   pre_calc_curve_g[65536];
    uint16_t   pre_calc_curve_b[65536];
    uint16_t   pre_calc_levels[65536]; /* For black level and white level */
    uint16_t   pre_calc_gamma[65536];
    uint16_t   pre_calc_gamma_gradient[65536];
    uint32_t   pre_calc_sharp_a[65536];
    uint16_t   pre_calc_sharp_x[65536]; /* In horizontal dimension */
    uint16_t   pre_calc_sharp_y[65536]; /* In vertical dimension */
    /* Precalculated values for saturation */
    int32_t    pre_calc_sat[131072];
    /* Precalculated values for vibrance */
    int32_t    pre_calc_vibrance[131072];

    /* Transformation */
    uint8_t    transformation;

    /* Pointer to dual iso variable from llrawproc, needed for highest green analysis */
    int        *dual_iso;

    /* 2D Median Denoiser */
    uint8_t    denoiserWindow;
    uint8_t    denoiserStrength;

    /* RBF Denoiser */
    uint8_t    rbfDenoiserLuma;
    uint8_t    rbfDenoiserChroma;
    uint8_t    rbfDenoiserRange;

    /* Grain Generator */
    uint8_t    grainStrength;

    /* Gradient */
    double     gradient_exposure_stops;
    double     gradient_contrast;
    double     gradient_contrast_curve[65536];
    uint8_t    gradient_enable;
    uint16_t * gradient_mask; //same size like picture, alpha mask

    /* Vignette */
    int8_t     vignette_strength;
    float    * vignette_mask; //same size like picture, alpha mask
    float    * vignette_end;

    /* Use Camera Matrix */
    uint8_t    use_cam_matrix;
    uint8_t    colour_gamut;
    uint8_t    tonemap_function;

    /* Allow creative adjustments with log profile */
    uint8_t    allow_creative_adjustments;

    /* CA filter */
    uint8_t ca_desaturate; /* Range 0..100 */
    uint8_t ca_radius; /* Range 0.. */
} processingObject_t;

#endif
