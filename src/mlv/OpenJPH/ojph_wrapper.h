#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

void* ojph_encoder_new();
void  ojph_encoder_set_image(void* e, uint32_t w, uint32_t h,
                             uint32_t num_comps, uint32_t bit_depth,
                             int is_signed);
void  ojph_encoder_set_lossless(void* e, int lossless);
void  ojph_encoder_set_decompositions(void* e, uint32_t n);
void  ojph_encoder_set_quantization(void* e, float q);
size_t ojph_encoder_encode_into(void* e, const int32_t* pixels,
                                uint8_t* out_buf, size_t out_buf_size);
void  ojph_encoder_free(void* e);

void* ojph_decoder_new();
int   ojph_decoder_probe(void* d, const uint8_t* data, size_t size,
                         uint32_t* w, uint32_t* h,
                         uint32_t* num_comps, uint32_t* bit_depth,
                         int* is_signed);
size_t ojph_decoder_decode_into(void* d, const uint8_t* data, size_t size,
                                int32_t* out_pixels, size_t out_pixels_cap,
                                uint32_t* out_w, uint32_t* out_h,
                                uint32_t* out_num_comps);
void  ojph_decoder_free(void* d);

#ifdef __cplusplus
}
#endif
