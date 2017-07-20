#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "../../src/imageio/imageio.h"

/* Main video reader */
#include "../../src/mlv_include.h"

/* Very global */
mlvObject_t * video;
processingObject_t * processingsettings;

float sum(float * array, int count)
{
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        sum += array[i];
    }
    return sum;
}

int main()
{
    /* Can be any clip (cropmode/fullressilent is best) */
    video = initMlvObjectWithClip("../../../MLV_CLIP.mlv");
    processingsettings = initProcessingObject();

    /* Link video with processing settings */
    setMlvProcessing(video, processingsettings);

    int original_width = getMlvWidth(video);
    int original_height = getMlvHeight(video);

    int pixel_size = original_width * original_height;

    float * frame = malloc( sizeof(float) * pixel_size );

    /* Just use frame 2 */
    getMlvRawFrameFloat(video, 2, frame);


    /* Downscale (5D2 and 5D3 method), for comparison */
    int third_width = original_width / 3;
    int third_height = original_height / 3;
    int third_frame_size = third_width * third_height;

    /* 1/3 size */
    float * small_frame_5d2 = malloc( sizeof(float) * third_frame_size );
    float * small_frame_5d3 = malloc( sizeof(float) * third_frame_size );

    /* binning weights */
    float bw_5d3[] = { 1.0f, 1.0f, 1.0f,
                       1.0f, 1.0f, 1.0f,
                       1.0f, 1.0f, 1.0f };
    
    float bw_5d2[] = { 0.0f, 0.0f, 0.0f,
                       1.0f, 1.0f, 1.0f,
                       0.0f, 0.0f, 0.0f, };

    for (int y = 0; y < third_height; ++y)
    {
        /* Location */
        int loc_y = y * third_width;

        int input_loc_y = y * 3 * original_width;

        for (int x = 0; x < third_width; ++x)
        {
            float * input_pix = frame + (input_loc_y + x * 3);
            float * input_pix_above = input_pix - (original_width * 2);
            float * input_pix_below = input_pix + (original_width * 2);

            float * output_5d2 = small_frame_5d2 + (loc_y + x);
            float * output_5d3 = small_frame_5d3 + (loc_y + x);

            /* Bad binning */
            * output_5d2 = (
                (input_pix_above[0] * bw_5d2[0] + input_pix_above[-2] * bw_5d2[1] + input_pix_above[2] * bw_5d2[2])
                 + (input_pix[0] * bw_5d2[3]    +    input_pix[-2] * bw_5d2[4]    +    input_pix[2] * bw_5d2[5]) +
                (input_pix_below[0] * bw_5d2[6] + input_pix_below[-2] * bw_5d2[7] + input_pix_below[2] * bw_5d2[8])
            ) / sum(bw_5d2, 9);
            /* Good binning */
            * output_5d3 = (
                (input_pix_above[0] * bw_5d3[0] + input_pix_above[-2] * bw_5d3[1] + input_pix_above[2] * bw_5d3[2])
                 + (input_pix[0] * bw_5d3[3]    +    input_pix[-2] * bw_5d3[4]    +    input_pix[2] * bw_5d3[5]) +
                (input_pix_below[0] * bw_5d3[6] + input_pix_below[-2] * bw_5d3[7] + input_pix_below[2] * bw_5d3[8])
            ) / sum(bw_5d3, 9);

            /* Just to make the image easier to look at.... speed doesnt matter ok */
            * output_5d2 -= getMlvBlackLevel(video) * 4;
            * output_5d3 -= getMlvBlackLevel(video) * 4;

            * output_5d3 /= 65535.0f;
            * output_5d2 /= 65535.0f;

            * output_5d3 = powf(* output_5d3, 0.45);
            * output_5d2 = powf(* output_5d2, 0.45);
            
            * output_5d3 *= 65535.0f;
            * output_5d2 *= 65535.0f;
        }
    }

    uint8_t * bitmap_small = malloc( third_frame_size * 3 * sizeof(uint8_t) );

    /* Pay zero attention to the 'imagestruct' and bitmap bits, it was pretty much my first C project */
    imagestruct small_image = { third_width, third_height, bitmap_small, 1 };

    /* Copy and save 5D2 */
    for (int i = 0; i < third_frame_size; ++i)
    {
        bitmap_small[i * 3] = small_frame_5d2[i] / 256;
        bitmap_small[i*3+1] = bitmap_small[i * 3];
        bitmap_small[i*3+2] = bitmap_small[i * 3]; 
    }

    write_bmp3_24(&small_image, "5D2.bmp");

    /* Copy and save 5D3 */
    for (int i = 0; i < third_frame_size; ++i)
    {
        bitmap_small[i * 3] = small_frame_5d3[i] / 256;
        bitmap_small[i*3+1] = bitmap_small[i * 3];
        bitmap_small[i*3+2] = bitmap_small[i * 3]; 
    }

    write_bmp3_24(&small_image, "5D3.bmp");


    /* Full res output */
    uint8_t * bitmap = malloc( pixel_size * 3 * sizeof(uint8_t) );
    for (int i = 0; i < pixel_size; ++i)
    {
        bitmap[i * 3] = frame[i] / 256;

        bitmap[i*3+1] = bitmap[i * 3];
        bitmap[i*3+2] = bitmap[i * 3]; 
    }

    /* Full res */
    imagestruct TestImage = { original_width, original_height, bitmap, 1 };
    write_bmp3_24(&TestImage, "output.bmp");

    freeMlvObject(video);
    freeProcessingObject(processingsettings);

    return 0;
}
