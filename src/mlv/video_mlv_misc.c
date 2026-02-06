#include <stdlib.h>
#include <stdint.h>

#include "llrawproc/llrawproc.h"
#include "../debayer/debayer.h"
#include "mlv_object.h"
#include "video_mlv.h"

int create_thumbnail(mlvObject_t * video, uint8_t * thumbnail_img, int downscaled_factor, int width, int height, int threads) {
    int raw_w = video->RAWI.xRes;
    int raw_h = video->RAWI.yRes;
    int i, j;


    uint16_t *raw_frame = (uint16_t *)(malloc(raw_w * raw_h * sizeof(uint16_t)));
    if (getMlvRawFrameUint16(video, 0, raw_frame)) {
      free(raw_frame);
      return 1;
    }

    int pixel_count = (width) * (height);

    uint16_t *downscaled_frame = (uint16_t *)(malloc(pixel_count * sizeof(uint16_t)));

    if (!downscaled_frame) {
      free(raw_frame);
      return 1;
    }

    for (i = 0; i < height; i++)
        for (j = 0; j < width; j++)
            downscaled_frame[i * width + j] = raw_frame[(i * downscaled_factor) * raw_w + (j * downscaled_factor)];

    int shift_val = (llrpHQDualIso(video)) ? 0 : (16 - video->RAWI.raw_info.bits_per_pixel);

    float *float_thumb = (float *)(malloc(pixel_count * sizeof(float)));

    if (!float_thumb) {
      free(raw_frame);
      free(downscaled_frame);
      return 1;
    }

    for (i = 0; i < pixel_count; i++)
        float_thumb[i] = (float)(downscaled_frame[i] << shift_val);

    uint16_t *debayered_frame = (uint16_t *)(malloc(pixel_count * 3 * sizeof(uint16_t)));

    if (!debayered_frame) {
      free(raw_frame);
      free(downscaled_frame);
      free(float_thumb);
      return 1;
    }

    debayerBasic(debayered_frame, float_thumb, width, height, 1);

    uint16_t *processed_frame = (uint16_t *)(malloc(pixel_count * 3 * sizeof(uint16_t)));

    if (!processed_frame) {
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
