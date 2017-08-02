#ifndef _processing_struct_
#define _processing_struct_

/* Processing settings structure (a mess) */
typedef struct {

    /* (RAW) white and black levels */
    int black_level, white_level;

    /* Do highlight reconstrucion? It's slow */
    int highlight_reconstruction;
    uint16_t highest_green; /* Used for reconstruction */

    /* Do tonemapping? */
    int tone_mapping;

    /* Double is classy */

    /* Camera's matrix - will need to be set on opening clip, default set for 5D Mark II */
    double xyz_to_cam_matrix[9];
    /* What is used to turn XYZ produced form previious matrix into RGB image */
    double xyz_to_rgb_matrix[9];
    /* Main matrix: combined white balance + exposure + whatever the cmaera matrix does */
    double final_matrix[9];
    /* Precalculated all matrix values 0-65535 */
    int32_t * pre_calc_matrix[9];

    /* White balance */
    double     kelvin; /* from 2500 to 10000 */
    double     wb_tint; /* from -10 to +10 PLEAZ */

    /* Generic processing things */
    double     exposure_stops; /* Make this -4 to +4 STOPS */
    double     saturation; /* Slider from 0 to 2, to power of log(base2)of 3.5, so 1 stays in the middle, but max is 3.5 */

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

    /* Gamma should be ~2.2 technically, but highger values look ok too */
    double     gamma_power;
    /* Limited to 0.0 - 0.6 range */
    double     lighten;

    /* For whitebalance */
    double     wb_multipliers[3];
    
    /* These are pre-calculated curves / values for contrast and stuff 
     * will be calculated on setting changes, values 0-65535 */
    uint16_t * pre_calc_curve_r;
    uint16_t * pre_calc_curve_g;
    uint16_t * pre_calc_curve_b;
    uint16_t * pre_calc_levels; /* For black level and white level */
    uint16_t * pre_calc_gamma;
    /* Precalculated values for saturation */
    int32_t  * pre_calc_sat;

} processingObject_t;

/* Maybe save edits to MLV file as a block? */
typedef struct {
    uint8_t    blockType[4]; /* "EDIT" */
    uint32_t   blockSize;    /* total block size */
    double     kelvin;
    double     wb_tint;
    double     exposure_stops;
    double     saturation;
    double     light_contrast_factor;
    double     light_contrast_range;
    double     dark_contrast_factor;
    double     dark_contrast_range;
    double     gamma_power;
    double     lighten;
}  mlv_edit_hdr_t;

#endif