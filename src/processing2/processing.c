#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../matrix/matrix.h"

#include "processing.h"

/* YLUT Maps input Y value to a new value and a saturation factor to apply to the pixel */
typedef struct {
    float Y_fac;
    float sat_fac;
} YLUT_t;
#define YLUT_size 2048

struct Processing2
{
    int res_x;
    int res_y;

    struct {
        double black_level;
        double white_level;
        double mat_tungsten[9];
        double mat_daylight[9];
    } camera_info;

    float * src_image;

    float * pre_processed_image; /* With WL/BL and highlight reconstruction */
    float * blur_image; /* For highlights/shadows/clarity type stuff */

    float result_matrix; /* Main processing matrix to output colour gamut */
    YLUT_t result_YLUT[YLUT_size]; /* Y-based LUT up to Y=16 (index this using sqrt(Y)/4) */

    float y_matrix; /* Matrix for getting Y value */

    struct {
        double highlight_rolloff;
        double exposure;

        double saturation;

        double wb_kelvin;
        double wb_tint;

        double dark_strength;
        double dark_range;
        double light_strength;
        double light_range;
        double lighten;
    } parameters;
};

#define PROCESSING_IMAGE_SIZE (sizeof(float)*3*Processing->res_x*Processing->res_y)

Processing_t * new_Processing(int ResX, int ResY)
{
    Processing_t * Processing = malloc(sizeof(Processing_t));
    Processing->res_x = ResX;
    Processing->res_y = ResY;

    #define ALLOCATE_IMAGE malloc(PROCESSING_IMAGE_SIZE);
    Processing->src_image = ALLOCATE_IMAGE;
    Processing->pre_processed_image = ALLOCATE_IMAGE;
    Processing->blur_image = ALLOCATE_IMAGE;
    #undef ALLOCATE_IMAGE

    return Processing;
}

void delete_Processing(Processing_t * Processing)
{
    free(Processing->src_image);
    free(Processing->blur_image);
}

/* Should be vectorisable by compiler */
static inline void process_pixel(
    float CamR,
    float CamG,
    float CamB,
    float * OutR,
    float * OutG,
    float * OutB,
    float * restrict Matrix,
    float * restrict Y_matrix,
    YLUT_t * restrict YLUT,
    int LengthYLUT
) {
    float Y = (CamR*Y_matrix[0]) + (CamG*Y_matrix[1]) + (CamB*Y_matrix[2]);

    /* Already in output space */
    float R = Matrix[0]*CamR + Matrix[1]*CamG + Matrix[2]*CamB;
    float G = Matrix[3]*CamR + Matrix[4]*CamG + Matrix[5]*CamB;
    float B = Matrix[6]*CamR + Matrix[7]*CamG + Matrix[8]*CamB;

    /* Clamp negative values. TODO: do this in some wide colour gamut like prophotoRGB or alexa */
    if (R < 0.0f) R = 0.0f;
    if (G < 0.0f) G = 0.0f;
    if (B < 0.0f) B = 0.0f;

    /* Interpolate linearly */
    if (Y > 3.99f) Y = 3.99f; /* Using 3.99 as max means no need to check end bounds */
    if (Y < 0.0f) Y = 0.0f;

    /* Get saturation and contrast values from Y-based lookup table */
    float Y_index = (sqrtf(Y) / 4.0f) * (float)LengthYLUT;
    int Y_i = (int)Y_index;
    float fac1 = Y_index - Y_i;
    float fac2 = 1.0f - fac1;

    /* Factor for saturation and Y */
    float Y_fac = YLUT[Y_i].Y_fac * fac2 + YLUT[Y_i+1].Y_fac * fac1;
    float sat_fac = YLUT[Y_i].sat_fac * fac2 + YLUT[Y_i+1].sat_fac * fac1;

    /* Add saturation and contrast in linear RGB space, yes, technically
     * not the best, but... saturation result is exactly the same as using
     * the perceprual Luv space in terms of hue linearity, so... actually
     * better than LAB, just not quite as good as Jzazbz or IPT or Oklab */
    R = ((R-Y) * sat_fac + Y) * Y_fac;
    G = ((G-Y) * sat_fac + Y) * Y_fac;
    B = ((B-Y) * sat_fac + Y) * Y_fac;

    /* Apply LUT here */

    *OutR = R;
    *OutG = G;
    *OutB = B;
}


void ProcessingDoProcessing32(Processing_t * Processing, float * Out)
{
    return;
}

void ProcessingDoProcessing16(Processing_t * Processing, uint16_t * Out)
{
    return;
}

void ProcessingDoProcessing8(Processing_t * Processing, uint8_t * Out)
{
    return;
}

int ProcessingGetResX(Processing_t * Processing)
{
    return Processing->res_x;
}

int ProcessingGetResY(Processing_t * Processing)
{
    return Processing->res_y;
}

void ProcessingSetInputImage(Processing_t * Processing, float * Image)
{
    memcpy(Processing->src_image, Image, PROCESSING_IMAGE_SIZE);
}

void ProcessingSetBlackLevel(Processing_t * Processing, double BlackLevel)
{
    Processing->camera_info.black_level = BlackLevel;
}

void ProcessingSetWhiteLevel(Processing_t * Processing, double WhiteLevel)
{
    Processing->camera_info.white_level = WhiteLevel;
}

void ProcessingSetCameraMatrix( Processing_t * Processing,
                                double * MatrixDaylight, 
                                double * MatrixTungsten )
{
    for (int i = 0; i < 9; ++i)
    {
        Processing->camera_info.mat_daylight[i] = MatrixDaylight[i];
        Processing->camera_info.mat_tungsten[i] = MatrixTungsten[i];
    }
}

void ProcessingSetExposure(Processing_t * Processing, double ExposureStops) { Processing->parameters.exposure = ExposureStops; }
double ProcessingGetExposure(Processing_t * Processing) { return Processing->parameters.exposure; }

void ProcessingSetHighlightRolloff(Processing_t * Processing, double Rolloff) { Processing->parameters.highlight_rolloff = Rolloff; }
double ProcessingGetHighlightRolloff(Processing_t * Processing) { return Processing->parameters.highlight_rolloff; }

void ProcessingSetWBKelvin(Processing_t * Processing, double Kelvin) { Processing->parameters.wb_kelvin = Kelvin; }
double ProcessingGetWBKelvin(Processing_t * Processing) { return Processing->parameters.wb_kelvin; }

void ProcessingSetWBTint(Processing_t * Processing, double Tint) { Processing->parameters.wb_tint = Tint; }
double ProcessingGetWBTint(Processing_t * Processing) { return Processing->parameters.wb_tint; }

void ProcessingSetWBFromImage(Processing_t * Processing, int X, int Y, int Radius) {return;}

void ProcessingSetSaturation(Processing_t * Processing, double Saturation) { Processing->parameters.saturation = Saturation; }
double ProcessingGetSaturation(Processing_t * Processing) {return 0.0;}

void ProcessingSetContrast( Processing_t * Processing, 
                            double DCRange,
                            double DCFactor,
                            double LCRange,
                            double LCFactor,
                            double Lighten )
{
    Processing->parameters.dark_range = DCRange;
    Processing->parameters.dark_strength = DCFactor;
    Processing->parameters.light_range = LCRange;
    Processing->parameters.light_strength = LCFactor;
    Processing->parameters.lighten = Lighten;
}

void ProcessingSetDCRange(Processing_t * Processing, double DCRange) {
    Processing->parameters.dark_range = DCRange;
}
void ProcessingSetDCFactor(Processing_t * Processing, double DCFactor) {
    Processing->parameters.dark_strength = DCFactor;
}
void ProcessingSetLCRange(Processing_t * Processing, double LCRange) {
    Processing->parameters.light_range = LCRange;
}
void ProcessingSetLCFactor(Processing_t * Processing, double LCFactor) {
    Processing->parameters.light_strength = LCFactor;
}
void ProcessingSetLightening(Processing_t * Processing, double Lighten) {
    Processing->parameters.lighten = Lighten;
}