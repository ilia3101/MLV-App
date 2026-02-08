#include <stdlib.h>
#include <stdint.h>

#include "llrawproc/llrawproc.h"
#include "../debayer/debayer.h"
#include "mlv_object.h"
#include "video_mlv.h"

int create_thumbnail(mlvObject_t * video, uint8_t * thumbnail_img, int downscaled_factor, int width, int height, int threads)
{
    int raw_w = video->RAWI.xRes;
    int raw_h = video->RAWI.yRes;
    int i, j;

    uint16_t *raw_frame = (uint16_t *)(malloc(raw_w * raw_h * sizeof(uint16_t)));
    if (getMlvRawFrameUint16(video, 0, raw_frame))
    {
        free(raw_frame);
        return 1;
    }

    int pixel_count = (width) * (height);

    uint16_t *downscaled_frame = (uint16_t *)(malloc(pixel_count * sizeof(uint16_t)));

    if (!downscaled_frame)
    {
        free(raw_frame);
        return 1;
    }

    for (i = 0; i < height; i++)
        for (j = 0; j < width; j++)
            downscaled_frame[i * width + j] = raw_frame[(i * downscaled_factor) * raw_w + (j * downscaled_factor)];

    int shift_val = (llrpHQDualIso(video)) ? 0 : (16 - video->RAWI.raw_info.bits_per_pixel);

    float *float_thumb = (float *)(malloc(pixel_count * sizeof(float)));

    if (!float_thumb)
    {
        free(raw_frame);
        free(downscaled_frame);
        return 1;
    }

    for (i = 0; i < pixel_count; i++)
        float_thumb[i] = (float)(downscaled_frame[i] << shift_val);

    uint16_t *debayered_frame = (uint16_t *)(malloc(pixel_count * 3 * sizeof(uint16_t)));

    if (!debayered_frame)
    {
        free(raw_frame);
        free(downscaled_frame);
        free(float_thumb);
        return 1;
    }

    debayerBasic(debayered_frame, float_thumb, width, height, 1);

    uint16_t *processed_frame = (uint16_t *)(malloc(pixel_count * 3 * sizeof(uint16_t)));

    if (!processed_frame)
    {
        free(raw_frame);
        free(downscaled_frame);
        free(debayered_frame);
        free(float_thumb);
        return 1;
    }

    applyProcessingObject(video->processing,
                          width, height,
                          debayered_frame,
                          processed_frame,
                          threads, 1, 0);

    for (i = 0; i < pixel_count * 3; i++)
        thumbnail_img[i] = (uint8_t)(processed_frame[i] >> 8);

    free(raw_frame);
    free(downscaled_frame);
    free(debayered_frame);
    free(float_thumb);
    free(processed_frame);

    return 0;
}

void get_area_average_downscale_thumnail (
    mlvObject_t *video,
    int frame_index, int downscale_factor,
    int cpu_cores,
    unsigned char *out_buffer) {

    if (!video || !out_buffer) {
        return;
    }

    /* Get RAW frame info */
    int raw_w = video->RAWI.xRes;
    int raw_h = video->RAWI.yRes;

    if (raw_w <= 0 || raw_h <= 0) {
        return;
    }

    /* Allocate memory for the full raw frame */
    float *raw_frame = (float *) malloc(raw_w * raw_h * sizeof(float));
    if (!raw_frame) {
        return;
    }

    /* Get the float B&W raw bayer data */
    getMlvRawFrameFloat(video, frame_index, raw_frame);

    uint16_t *debayered_raw_frame = (uint16_t *) malloc(
        (size_t) (raw_w * raw_h * 3) * sizeof(uint16_t));
    if (!debayered_raw_frame) {
        free(raw_frame);
        return;
    }

    /* get debayered image */
    debayerBasic(debayered_raw_frame, raw_frame, raw_w, raw_h, 1);

    const int thumbW = raw_w / downscale_factor;
    const int thumbH = raw_h / downscale_factor;

    uint16_t *downscaled_image = (uint16_t *) malloc(
        (size_t) (thumbW * thumbH * 3) * sizeof(uint16_t));
    if (!downscaled_image) {
        free(raw_frame);
        free(debayered_raw_frame);
        return;
    }

    /* Downscale */
    for (int outY = 0; outY < thumbH; ++outY) {
        for (int outX = 0; outX < thumbW; ++outX) {
            uint64_t sum_r = 0;
            uint64_t sum_g = 0;
            uint64_t sum_b = 0;

            int start_y = outY * downscale_factor;
            int start_x = outX * downscale_factor;

            for (int j = 0; j < downscale_factor; j++) {
                for (int i = 0; i < downscale_factor; i++) {
                    size_t pixel_index = ((size_t) (start_y + j) * raw_w + (start_x + i)) * 3;
                    sum_r += debayered_raw_frame[pixel_index + 0];
                    sum_g += debayered_raw_frame[pixel_index + 1];
                    sum_b += debayered_raw_frame[pixel_index + 2];
                }
            }

            size_t out_pixel_index = ((size_t) outY * thumbW + outX) * 3;
            downscaled_image[out_pixel_index + 0] = (uint16_t) (sum_r / (downscale_factor *
                                                                         downscale_factor));
            downscaled_image[out_pixel_index + 1] = (uint16_t) (sum_g / (downscale_factor *
                                                                         downscale_factor));
            downscaled_image[out_pixel_index + 2] = (uint16_t) (sum_b / (downscale_factor *
                                                                         downscale_factor));
        }
    }

    uint16_t *downscaled_processed_image = (uint16_t *) malloc(
        (size_t) (thumbW * thumbH * 3) * sizeof(uint16_t));
    if (!downscaled_processed_image) {
        free(raw_frame);
        free(debayered_raw_frame);
        free(downscaled_image);
        return;
    }

    applyProcessingObject(video->processing,
                          thumbW, thumbH,
                          downscaled_image,
                          downscaled_processed_image,
                          cpu_cores, 1, frame_index);

    size_t size = thumbW * thumbH * 3;
    for (size_t i = 0; i < size; i++) {
        out_buffer[i] = downscaled_processed_image[i] >> 8;
    }

    /* Cleanup */
    free(downscaled_processed_image);
    free(downscaled_image);
    free(debayered_raw_frame);
    free(raw_frame);
}
