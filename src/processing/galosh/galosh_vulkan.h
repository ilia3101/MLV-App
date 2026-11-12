/*!
 * \file galosh_vulkan.h
 * \brief Optional Vulkan backend for the GALOSH RAW denoiser
 */

#ifndef GALOSH_VULKAN_H
#define GALOSH_VULKAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Directory containing the precompiled GALOSH .spv shaders. */
void galosh_vulkan_set_shader_dir(const char *dir);

/* Returns 1 if the Vulkan backend is compiled in and a usable device exists. */
int galosh_vulkan_available(void);

/* Denoise a padded even-sized float Bayer frame.
 * Returns 0 on success, non-zero on error. */
int galosh_vulkan_denoise(const float *in, float *out,
                          int width, int height,
                          float luma_strength, float chroma_strength,
                          int fast,
                          float alpha, float sigma_sq,
                          int have_noise_model);

void galosh_vulkan_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* GALOSH_VULKAN_H */
