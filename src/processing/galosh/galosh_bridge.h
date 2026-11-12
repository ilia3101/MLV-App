/*!
 * \file galosh_bridge.h
 * \brief MLV-App bridge for the GALOSH RAW Bayer denoiser
 *
 * GALOSH is a blind, training-free denoiser.  The RAW variant operates on
 * linear Bayer data in [0,1].  This bridge converts MLV-App's 16-bit raw
 * float frame to that domain, runs the selected backend, and converts back.
 */

#ifndef GALOSH_BRIDGE_H
#define GALOSH_BRIDGE_H

#include <stdint.h>

#include "../processing_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Backend identifiers stored in processingObject_t::galoshBackend */
#define GALOSH_BACKEND_GPU 0
#define GALOSH_BACKEND_CPU 1

/* Returns 1 if a usable Vulkan backend is compiled in and a device exists. */
int galosh_vulkan_available(void);

/* Frees any global backend resources. */
void galosh_shutdown(void);

/* Denoise a 16-bit-scale linear Bayer frame in place.
 * Returns 0 on success, non-zero if the frame was left untouched.
 *
 * anamorphic_x > 1 runs the denoiser on a horizontally upsampled Bayer grid
 * and samples the result back to the original positions.  Only odd factors
 * preserve the 2x2 CFA parity; 3 is used for 1x3 MLV footage.  The noise
 * model is always estimated on the original grid. */
int galosh_bayer_denoise_ex(processingObject_t *processing,
                            float *frame,
                            int width, int height,
                            uint32_t cfa_pattern,
                            float black_level,
                            float white_level,
                            uint64_t frame_index,
                            int anamorphic_x);

/* Convenience wrapper for anamorphic_x = 1. */
int galosh_bayer_denoise(processingObject_t *processing,
                         float *frame,
                         int width, int height,
                         uint32_t cfa_pattern,
                         float black_level,
                         float white_level,
                         uint64_t frame_index);

#ifdef __cplusplus
}
#endif

#endif /* GALOSH_BRIDGE_H */
