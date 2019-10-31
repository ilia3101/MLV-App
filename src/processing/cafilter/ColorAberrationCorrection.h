#ifndef COLORABBERATIONCORRECTION
#define COLORABBERATIONCORRECTION

void CACorrection(int imageX, int imageY,
                  uint16_t * __restrict inputImage,
                  uint16_t * __restrict outputImage,
                  uint16_t threshold);

#endif
