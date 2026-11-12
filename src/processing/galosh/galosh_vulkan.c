/*!
 * \file galosh_vulkan.c
 * \brief Optional Vulkan backend for the GALOSH RAW denoiser
 *
 * The full Vulkan host is intentionally isolated behind this small API so the
 * normal MLV-App build does not require a Vulkan SDK.  When GALOSH_VULKAN is
 * not defined the bridge transparently uses the CPU backend.
 */

#include "galosh_vulkan.h"

#ifndef GALOSH_VULKAN

void galosh_vulkan_set_shader_dir(const char *dir)
{
    (void)dir;
}

int galosh_vulkan_available(void)
{
    return 0;
}

int galosh_vulkan_denoise(const float *in, float *out,
                          int width, int height,
                          float luma_strength, float chroma_strength,
                          int fast,
                          float alpha, float sigma_sq,
                          int have_noise_model)
{
    (void)in; (void)out; (void)width; (void)height;
    (void)luma_strength; (void)chroma_strength; (void)fast;
    (void)alpha; (void)sigma_sq; (void)have_noise_model;
    return 1;
}

void galosh_vulkan_shutdown(void)
{
}

#endif /* !GALOSH_VULKAN */
