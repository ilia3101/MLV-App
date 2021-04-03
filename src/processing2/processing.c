#include <stdlib.h>
#include <string.h>

#include "../matrix/matrix.h"

#include "processing.h"

struct Processing2
{
    struct {
        double mat_1[9]; /* Tungsten */
        double mat_2[9]; /* Daylight */
    } camera_profile;

    int res_x;
    int res_y;

    float * src_image;

    float * pre_processed_image; /* With highlight reconstruction */
    float * blur_image; /* For highlights and stuff */
};

#define PROCESSING_IMAGE_SIZE (sizeof(float)*3*processing->res_x*processing->res_y)

Processing_t * new_Processing(int ResX, int ResY)
{
    Processing_t * processing = malloc(sizeof(Processing_t));
    processing->res_x = ResX;
    processing->res_y = ResY;

    #define ALLOCATE_IMAGE malloc(PROCESSING_IMAGE_SIZE);
    processing->src_image = ALLOCATE_IMAGE;
    processing->blur_image = ALLOCATE_IMAGE;
    #undef ALLOCATE_IMAGE

    return processing;
}

void delete_Processing(Processing_t * processing)
{
    free(processing->src_image);
    free(processing->blur_image);
}

void ProcessingDoProcessing32(float * Out)
{
    return;
}

void ProcessingDoProcessing16(uint16_t * Out)
{
    return;
}

void ProcessingDoProcessing8(uint16_t * Out)
{
    return;
}

int ProcessingGetResX(Processing_t * processing)
{
    return processing->res_x;
}

int ProcessingGetResY(Processing_t * processing)
{
    return processing->res_y;
}

void ProcessingSetInputImage(Processing_t * processing, float * Image)
{
    memcpy(processing->src_image, Image, PROCESSING_IMAGE_SIZE);
}
