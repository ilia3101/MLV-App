/* Some functions that are used within raw_processing.c
 * This file is directly #included in raw_processing.c */


/* Measurements taken from 5D Mark II RAW photos using EXIFtool, surely Canon can't be wrong about WB mutipliers? */
static int wb_kelvin[]   = {  2500,  3000,  3506,  4000,  4503,  5011,  5517,  6018,  6509,  7040,  7528,  8056,  8534,  9032,  9531, 10000 };
static double wb_red[]   = { 1.349, 1.596, 1.731, 1.806, 1.954, 2.081, 2.197, 2.291, 2.365, 2.444, 2.485, 2.528, 2.566, 2.612, 2.660, 2.702 };
static double wb_green[] = { 1.137, 1.112, 1.056, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000, 1.000 };
static double wb_blue[]  = { 3.985, 3.184, 2.524, 2.103, 1.903, 1.760, 1.641, 1.542, 1.476, 1.414, 1.390, 1.363, 1.333, 1.296, 1.263, 1.229 };


/* Returns multipliers for white balance by (linearly) interpolating measured 
 * Canon values... stupidly simple, also range limited to 2500-10000 (pls obey) */
void get_kelvin_multipliers(double kelvin, double * multiplier_output)
{
    int k = 0;

    while (1 < 2)
    {
        if ( kelvin > wb_kelvin[k] && kelvin < wb_kelvin[k+1] ) 
        {
            break;
        }

        /* Doesn't need interpolation */
        else if ( kelvin == wb_kelvin[k] )
        {
            multiplier_output[0] = wb_red[k];
            multiplier_output[1] = wb_green[k];
            multiplier_output[2] = wb_blue[k];
            return;
        }

        k++;
    }

    double diff1 = (double)wb_kelvin[k+1] - (double)wb_kelvin[k];
    double diff2 = (double)wb_kelvin[k+1] - kelvin;

    /* Weight between the two */
    double weight1 = diff2 / diff1;
    double weight2 = 1.0 - weight1;

    multiplier_output[0] = (   wb_red[k] * weight1) + (   wb_red[k+1] * weight2);
    multiplier_output[1] = ( wb_green[k] * weight1) + ( wb_green[k+1] * weight2);
    multiplier_output[2] = (  wb_blue[k] * weight1) + (  wb_blue[k+1] * weight2);
}


/* Adds contrast(S-curve) to a value in a unique(or not) way */
double add_contrast ( double pixel, /* Range 0.0 - 1.0 */
                      double dark_contrast_range, 
                      double dark_contrast_factor, 
                      double light_contrast_range, 
                      double light_contrast_factor )
{
    /* Adjust based on the range, to make it not seem like range also controls strength */
    double dc_factor = dark_contrast_factor / (dark_contrast_range * 2);
    double lc_factor = light_contrast_factor / (light_contrast_range * 2);

    if (pixel < dark_contrast_range) 
        pixel = pow(pixel, 1 + (dark_contrast_range - pixel) 
        * (dc_factor
        /* Effect is reduced closer to end of range for a smooth curve: */
        * (1.0 - pixel/dark_contrast_range)) );

    pixel = 1.0 - pixel; /* Invert for contrasting the highlights */

    if (pixel < light_contrast_range && pixel > 0) 
        pixel = pow(pixel, 1 + (light_contrast_range - pixel) 
        * (lc_factor 
        * (1.0 - pixel/light_contrast_range)) );

    pixel = 1.0 - pixel; /* Invert back */

    return pixel;
}


/* Calculates the final matrix (processing->main_matrix), and precalculates all the values;
 * Combines: exposure + white balance + camera specific adjustment all in one matrix! */
void processing_update_matrices(processingObject_t * processing)
{
    /* Temporary working matrix */
    double temp_matrix_a[9];

    /* (for shorter code) */
    int32_t ** pm = processing->pre_calc_matrix;

    /* Exposure */
    double exposure_factor = pow(2.0, processing->exposure_stops);

    /* Camers and XYZ matrix stuff is doing nothing for now, all id matrices 
     * for now as I really got lost and confused and nothing was working */

    /* Create a camera to XYZ matrix in temp_matrix_a */
    invertMatrix(processing->xyz_to_cam_matrix, temp_matrix_a);

    /* 
     * Do whitebalance... 
     */

    /* Convert XYZ to eye cone space HERE (not done), XYZ will do for now */

    /* Multiply channels */
    for (int i = 0; i < 3; ++i) temp_matrix_a[i] *= processing->wb_multipliers[0];
    for (int i = 3; i < 6; ++i) temp_matrix_a[i] *= processing->wb_multipliers[1];
    for (int i = 6; i < 9; ++i) temp_matrix_a[i] *= processing->wb_multipliers[2];

    /* Convert back to XYZ space from cone space HERE (also not done) */

    /* Multiply the currently XYZ matrix back to RGB in to final_matrix */
    multiplyMatrices( temp_matrix_a,
                      processing->xyz_to_rgb_matrix,
                      processing->final_matrix );

    /* Apply exposure to all matrix values */
    for (int i = 0; i < 9; ++i) processing->final_matrix[i] *= exposure_factor;

    /* Matrix stuff done I guess */

    /* Precalculate 0-65535 */
    for (int i = 0; i < 9; ++i)
    {
        for (int j = 0; j < 65536; ++j)
        {
            pm[i][j] = (int32_t)((double)j * processing->final_matrix[i]);
        }
    }

    /* Highest green value - pixels at this value will need to be reconstructed */
    processing->highest_green = LIMIT16(MAX(pm[3][65535],pm[3][0]) + MAX(pm[4][65535],pm[4][0]) + MAX(pm[5][65535],pm[5][0]));

    /* This is nice */
    printMatrix(processing->final_matrix);

    /* done? */
}