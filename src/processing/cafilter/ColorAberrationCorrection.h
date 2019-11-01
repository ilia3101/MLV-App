#ifndef COLORABBERATIONCORRECTION
#define COLORABBERATIONCORRECTION

#include <stdlib.h>
#include <stdint.h>

extern void CACorrection(int imageX, int imageY,
                  uint16_t * __restrict inputImage,
                  uint16_t * __restrict outputImage,
                  uint16_t threshold, uint8_t radius);

#endif
