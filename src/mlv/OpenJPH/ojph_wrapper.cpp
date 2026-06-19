#include "ojph_wrapper.h"

#include "openjph/ojph_codestream.h"
#include "openjph/ojph_file.h"
#include "openjph/ojph_params.h"
#include "openjph/ojph_mem.h"

#include <cstring>
#include <new>

class fixed_mem_outfile : public ojph::outfile_base {
    uint8_t *buf;
    size_t   cap;
    size_t   len;

public:
    fixed_mem_outfile() : buf(nullptr), cap(0), len(0) {}

    void open(uint8_t *buffer, size_t capacity) {
        buf = buffer;
        cap = capacity;
        len = 0;
    }

    size_t write(const void *ptr, size_t size) override {
        if (len + size > cap) return 0;
        memcpy(buf + len, ptr, size);
        len += size;
        return size;
    }

    ojph::si64 tell() override { return (ojph::si64)len; }

    void close() override { buf = nullptr; cap = 0; len = 0; }

    size_t written() const { return len; }
};

struct Encoder {
    ojph::codestream cs;
    fixed_mem_outfile  out;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t num_comps = 1;
    uint32_t bit_depth = 8;
    int is_signed = 0;
    int lossless = 0;
    uint32_t decompositions = 5;
    float quantization = 0.0039f;

    void configure() {
        ojph::param_siz siz = cs.access_siz();
        siz.set_image_extent(ojph::point(w, h));
        siz.set_num_components(num_comps);
        for (uint32_t c = 0; c < num_comps; ++c)
            siz.set_component(c, ojph::point(1, 1), bit_depth, is_signed != 0);
        siz.set_image_offset(ojph::point(0, 0));
        siz.set_tile_size(ojph::size(0, 0));
        siz.set_tile_offset(ojph::point(0, 0));

        ojph::param_cod cod = cs.access_cod();
        cod.set_num_decomposition(decompositions);
        cod.set_block_dims(64, 64);
        cod.set_progression_order("RPCL");
        cod.set_color_transform(num_comps >= 3);
        cod.set_reversible(lossless != 0);

        if (!lossless)
            cs.access_qcd().set_irrev_quant(quantization);

        cs.set_planar(false);
    }
};

struct Decoder {
    ojph::codestream cs;
};

extern "C" {

void* ojph_encoder_new() {
    return new (std::nothrow) Encoder();
}

void ojph_encoder_set_image(void* e, uint32_t w, uint32_t h,
                            uint32_t num_comps, uint32_t bit_depth,
                            int is_signed) {
    auto* enc = static_cast<Encoder*>(e);
    enc->w = w; enc->h = h; enc->num_comps = num_comps;
    enc->bit_depth = bit_depth; enc->is_signed = is_signed;
}

void ojph_encoder_set_lossless(void* e, int lossless)  { static_cast<Encoder*>(e)->lossless = lossless; }
void ojph_encoder_set_decompositions(void* e, uint32_t n) { static_cast<Encoder*>(e)->decompositions = n; }
void ojph_encoder_set_quantization(void* e, float q)       { static_cast<Encoder*>(e)->quantization = q; }

size_t ojph_encoder_encode_into(void* e, const int32_t* pixels,
                                uint8_t* out_buf, size_t out_buf_size) {
    auto* enc = static_cast<Encoder*>(e);

    enc->cs.restart();
    enc->configure();

    enc->out.close();
    enc->out.open(out_buf, out_buf_size);

    enc->cs.write_headers(&enc->out);

    uint32_t w = enc->w, h = enc->h, nc = enc->num_comps;
    ojph::ui32 next_comp;
    ojph::line_buf* line = enc->cs.exchange(nullptr, next_comp);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t c = 0; c < nc; ++c) {
            ojph::si32* dp = line->i32;
            for (uint32_t x = 0; x < w; ++x)
                dp[x] = pixels[(y * w + x) * nc + c];
            line = enc->cs.exchange(line, next_comp);
        }
    }

    enc->cs.flush();

    size_t written = enc->out.written();
    enc->out.close();
    return written;
}

void ojph_encoder_free(void* e) { delete static_cast<Encoder*>(e); }

// --- Decoder ---

void* ojph_decoder_new() {
    return new (std::nothrow) Decoder();
}

int ojph_decoder_probe(void* d, const uint8_t* data, size_t size,
                       uint32_t* w, uint32_t* h,
                       uint32_t* num_comps, uint32_t* bit_depth,
                       int* is_signed) {
    auto* dec = static_cast<Decoder*>(d);
    dec->cs.restart();

    ojph::mem_infile infile;
    infile.open(data, size);
    dec->cs.enable_resilience();
    dec->cs.read_headers(&infile);

    ojph::param_siz siz = dec->cs.access_siz();
    *w = siz.get_recon_width(0);
    *h = siz.get_recon_height(0);
    *num_comps = siz.get_num_components();
    *bit_depth = siz.get_bit_depth(0);
    *is_signed = siz.is_signed(0) ? 1 : 0;

    infile.close();
    return 0;
}

size_t ojph_decoder_decode_into(void* d, const uint8_t* data, size_t size,
                                int32_t* out_pixels, size_t out_pixels_cap,
                                uint32_t* out_w, uint32_t* out_h,
                                uint32_t* out_num_comps) {
    auto* dec = static_cast<Decoder*>(d);
    dec->cs.restart();

    ojph::mem_infile infile;
    infile.open(data, size);

    dec->cs.enable_resilience();
    dec->cs.read_headers(&infile);
    dec->cs.set_planar(false);
    dec->cs.create();

    ojph::param_siz siz = dec->cs.access_siz();
    uint32_t w = siz.get_recon_width(0);
    uint32_t h = siz.get_recon_height(0);
    uint32_t nc = siz.get_num_components();
    size_t needed = (size_t)w * h * nc;

    if (needed > out_pixels_cap) {
        infile.close();
        return 0;
    }

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t c = 0; c < nc; ++c) {
            ojph::ui32 comp_num;
            ojph::line_buf* line = dec->cs.pull(comp_num);
            const ojph::si32* sp = line->i32;
            for (uint32_t x = 0; x < w; ++x)
                out_pixels[(y * w + x) * nc + c] = sp[x];
        }
    }

    *out_w = w;
    *out_h = h;
    *out_num_comps = nc;
    infile.close();
    return needed;
}

void ojph_decoder_free(void* d) { delete static_cast<Decoder*>(d); }

} // extern "C"
