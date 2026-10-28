/*
MIT License

Copyright (c) 2017 Ming

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Fixed bugs and adapted to RGB64 by masc4ii (c) 2019
Added dual threading by masc4ii (c) 2021
Optimized memory footprint and GCD parallelism (c) 2026
*/

#include "RBFilterPlain.h"
#include <algorithm>
#include <math.h>
#include <vector>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

using namespace std;

#define QX_DEF_U16_MAX 65535

CRBFilterPlain::CRBFilterPlain()
{
}

CRBFilterPlain::~CRBFilterPlain()
{
    releaseMemory();
}

// assumes 3/4 channel images, 1 byte per channel
void CRBFilterPlain::reserveMemory(int max_width, int max_height, int channels)
{
    // basic sanity check
    if(!(max_width >= 10 && max_width < 10000))return;
    if(!(max_height >= 10 && max_height < 10000))return;
    if(!(channels >= 1 && channels <= 4))return;

    releaseMemory();

    m_reserve_width = max_width;
    m_reserve_height = max_height;
    m_reserve_channels = channels;

    int width_height = m_reserve_width * m_reserve_height;
    int width_height_channel = width_height * m_reserve_channels;

    // Allocate only the needed buffers:
    // m_left_pass_color serves as the full-frame intermediate horizontal color buffer
    m_left_pass_color = new float[width_height_channel];
    m_left_pass_factor = nullptr;

    m_right_pass_color = nullptr;
    m_right_pass_factor = nullptr;

    m_down_pass_color = new float[width_height_channel]();
    m_down_pass_factor = new float[width_height]();

    m_up_pass_color = new float[width_height_channel]();
    m_up_pass_factor = new float[width_height]();
}

void CRBFilterPlain::releaseMemory()
{
    m_reserve_width = 0;
    m_reserve_height = 0;
    m_reserve_channels = 0;

    if (m_left_pass_color)
    {
        delete[] m_left_pass_color;
        m_left_pass_color = nullptr;
    }

    if (m_left_pass_factor)
    {
        delete[] m_left_pass_factor;
        m_left_pass_factor = nullptr;
    }

    if (m_right_pass_color)
    {
        delete[] m_right_pass_color;
        m_right_pass_color = nullptr;
    }

    if (m_right_pass_factor)
    {
        delete[] m_right_pass_factor;
        m_right_pass_factor = nullptr;
    }

    if (m_down_pass_color)
    {
        delete[] m_down_pass_color;
        m_down_pass_color = nullptr;
    }

    if (m_down_pass_factor)
    {
        delete[] m_down_pass_factor;
        m_down_pass_factor = nullptr;
    }

    if (m_up_pass_color)
    {
        delete[] m_up_pass_color;
        m_up_pass_color = nullptr;
    }

    if (m_up_pass_factor)
    {
        delete[] m_up_pass_factor;
        m_up_pass_factor = nullptr;
    }
}

int CRBFilterPlain::getDiffFactor(const uint16_t *color1, const uint16_t *color2) const
{
    int final_diff;
    int component_diff[4];

    // find absolute difference between each component
    for (int i = 0; i < m_reserve_channels; i++)
    {
        component_diff[i] = abs((int)color1[i] - (int)color2[i]);
    }

    // based on number of components, produce a single difference value in the 0-QX_DEF_U16_MAX range
    switch (m_reserve_channels)
    {
    case 1:
        final_diff = component_diff[0];
        break;

    case 2:
        final_diff = ((component_diff[0] + component_diff[1]) >> 1);
        break;

    case 3:
        final_diff = ((component_diff[0] + component_diff[2]) >> 2) + (component_diff[1] >> 1);
        break;

    case 4:
        final_diff = ((component_diff[0] + component_diff[1] + component_diff[2] + component_diff[3]) >> 2);
        break;

    default:
        final_diff = 0;
    }

    if( final_diff < 0 ) final_diff = 0;
    if( final_diff > QX_DEF_U16_MAX ) final_diff = QX_DEF_U16_MAX;

    return final_diff;
}

// memory must be reserved before calling image filter
void CRBFilterPlain::filter(uint16_t* img_src, uint16_t* img_dst,
    float sigma_spatial, float sigma_range,
    int width, int height, int channel)
{
    if(!img_src) return;
    if(!img_dst) return;
    if(!(m_reserve_channels == channel)) return;
    if(!(m_reserve_width >= width)) return;
    if(!(m_reserve_height >= height)) return;

    // compute a lookup table
    float alpha_f = static_cast<float>(exp(-sqrt(2.0) / (sigma_spatial * QX_DEF_U16_MAX)));
    float inv_alpha_f = 1.f - alpha_f;

    float range_table_f[QX_DEF_U16_MAX + 1];
    float inv_sigma_range = 1.0f / (sigma_range * QX_DEF_U16_MAX);
    for (int i = 0; i <= QX_DEF_U16_MAX; i++)
    {
        float ii = -i;
        range_table_f[i] = alpha_f * exp(ii * inv_sigma_range);
    }
    const float* const range_tbl = range_table_f;

    int width_height = width * height;
    float* horiz_color = m_left_pass_color;

    // Parallel Horizontal Pass using L1/L2 row scratchpads
#ifdef __APPLE__
    int workers = (height < 8) ? height : 8;
    dispatch_apply(workers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t chunk) {
        int y_start = (int)((chunk * height) / workers);
        int y_end   = (int)(((chunk + 1) * height) / workers);

        std::vector<float> row_left_color(width * channel);
        std::vector<float> row_left_factor(width);
        std::vector<float> row_right_color(width * channel);
        std::vector<float> row_right_factor(width);

        for (int y = y_start; y < y_end; y++)
        {
            const uint16_t* src_row = img_src + ((size_t)y * width * channel);
            float* dst_row = horiz_color + ((size_t)y * width * channel);

            bool has_left = (y < height - 1);
            bool has_right = (y > 0);

            if (has_left)
            {
                const uint16_t* src_prev = src_row;
                const uint16_t* s = src_row;
                float* lf = row_left_factor.data();
                float* lc = row_left_color.data();
                const float* prev_f = lf;
                const float* prev_c = lc;

                *lf++ = 1.f;
                for (int c = 0; c < channel; c++) *lc++ = *s++;

                for (int x = 1; x < width; x++)
                {
                    int diff = getDiffFactor(s, src_prev);
                    src_prev = s;
                    float a = range_tbl[diff];
                    *lf++ = inv_alpha_f + a * (*prev_f++);
                    for (int c = 0; c < channel; c++)
                    {
                        *lc++ = inv_alpha_f * (*s++) + a * (*prev_c++);
                    }
                }
            }
            else
            {
                std::fill(row_left_factor.begin(), row_left_factor.end(), 0.0f);
                std::fill(row_left_color.begin(), row_left_color.end(), 0.0f);
            }

            if (has_right)
            {
                const uint16_t* s = src_row + (width * channel - 1);
                float* rf = row_right_factor.data() + (width - 1);
                float* rc = row_right_color.data() + (width * channel - 1);
                const float* prev_f = rf;
                const float* prev_c = rc;

                *rf-- = 1.f;
                for (int c = 0; c < channel; c++) *rc-- = *s--;

                for (int x = 1; x < width; x++)
                {
                    int diff = getDiffFactor(s, s - 3);
                    float a = range_tbl[diff];
                    *rf-- = inv_alpha_f + a * (*prev_f--);
                    for (int c = 0; c < channel; c++)
                    {
                        *rc-- = inv_alpha_f * (*s--) + a * (*prev_c--);
                    }
                }
            }
            else
            {
                std::fill(row_right_factor.begin(), row_right_factor.end(), 0.0f);
                std::fill(row_right_color.begin(), row_right_color.end(), 0.0f);
            }

            for (int x = 0; x < width; x++)
            {
                float factor = 1.f / (row_left_factor[x] + row_right_factor[x]);
                for (int c = 0; c < channel; c++)
                {
                    int idx = x * channel + c;
                    dst_row[idx] = factor * (row_left_color[idx] + row_right_color[idx]);
                }
            }
        }
    });
#else
    #pragma omp parallel
    {
        std::vector<float> row_left_color(width * channel);
        std::vector<float> row_left_factor(width);
        std::vector<float> row_right_color(width * channel);
        std::vector<float> row_right_factor(width);

        #pragma omp for schedule(static)
        for (int y = 0; y < height; y++)
        {
            const uint16_t* src_row = img_src + ((size_t)y * width * channel);
            float* dst_row = horiz_color + ((size_t)y * width * channel);

            bool has_left = (y < height - 1);
            bool has_right = (y > 0);

            if (has_left)
            {
                const uint16_t* src_prev = src_row;
                const uint16_t* s = src_row;
                float* lf = row_left_factor.data();
                float* lc = row_left_color.data();
                const float* prev_f = lf;
                const float* prev_c = lc;

                *lf++ = 1.f;
                for (int c = 0; c < channel; c++) *lc++ = *s++;

                for (int x = 1; x < width; x++)
                {
                    int diff = getDiffFactor(s, src_prev);
                    src_prev = s;
                    float a = range_tbl[diff];
                    *lf++ = inv_alpha_f + a * (*prev_f++);
                    for (int c = 0; c < channel; c++)
                    {
                        *lc++ = inv_alpha_f * (*s++) + a * (*prev_c++);
                    }
                }
            }
            else
            {
                std::fill(row_left_factor.begin(), row_left_factor.end(), 0.0f);
                std::fill(row_left_color.begin(), row_left_color.end(), 0.0f);
            }

            if (has_right)
            {
                const uint16_t* s = src_row + (width * channel - 1);
                float* rf = row_right_factor.data() + (width - 1);
                float* rc = row_right_color.data() + (width * channel - 1);
                const float* prev_f = rf;
                const float* prev_c = rc;

                *rf-- = 1.f;
                for (int c = 0; c < channel; c++) *rc-- = *s--;

                for (int x = 1; x < width; x++)
                {
                    int diff = getDiffFactor(s, s - 3);
                    float a = range_tbl[diff];
                    *rf-- = inv_alpha_f + a * (*prev_f--);
                    for (int c = 0; c < channel; c++)
                    {
                        *rc-- = inv_alpha_f * (*s--) + a * (*prev_c--);
                    }
                }
            }
            else
            {
                std::fill(row_right_factor.begin(), row_right_factor.end(), 0.0f);
                std::fill(row_right_color.begin(), row_right_color.end(), 0.0f);
            }

            for (int x = 0; x < width; x++)
            {
                float factor = 1.f / (row_left_factor[x] + row_right_factor[x]);
                for (int c = 0; c < channel; c++)
                {
                    int idx = x * channel + c;
                    dst_row[idx] = factor * (row_left_color[idx] + row_right_color[idx]);
                }
            }
        }
    }
#endif

    // Vertical pass: Down pass and Up pass run concurrently
#ifdef __APPLE__
    dispatch_group_t group = dispatch_group_create();
    dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        const float* src_color_hor = horiz_color;
        const uint16_t* src_color = img_src;
        float* down_pass_color = m_down_pass_color;
        float* down_pass_factor = m_down_pass_factor;

        const uint16_t* src_prev = src_color;
        const float* prev_color = down_pass_color;
        const float* prev_factor = down_pass_factor;

        for (int x = 0; x < width; x++)
        {
            *down_pass_factor++ = 1.f;
            for (int c = 0; c < channel; c++) *down_pass_color++ = *src_color_hor++;
            src_color += channel;
        }

        for (int y = 1; y < height; y++)
        {
            for (int x = 0; x < width-1; x++)
            {
                int diff = getDiffFactor(src_color, src_prev);
                src_prev += channel;
                src_color += channel;

                float a = range_tbl[diff];
                *down_pass_factor++ = inv_alpha_f + a * (*prev_factor++);
                for (int c = 0; c < channel; c++)
                {
                    *down_pass_color++ = inv_alpha_f * (*src_color_hor++) + a * (*prev_color++);
                }
            }
        }
    });

    dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        int last_index = width * height * channel - 1;
        const uint16_t* src_color = img_src + last_index;
        const float* src_color_hor = horiz_color + last_index;
        float* up_pass_color = m_up_pass_color + last_index;
        float* up_pass_factor = m_up_pass_factor + (width * height - 1);

        const float* prev_color = up_pass_color;
        const float* prev_factor = up_pass_factor;

        for (int x = 0; x < width; x++)
        {
            *up_pass_factor-- = 1.f;
            for (int c = 0; c < channel; c++) *up_pass_color-- = *src_color_hor--;
            src_color -= channel;
        }

        for (int y = 1; y < height; y++)
        {
            for (int x = 0; x < width-1; x++)
            {
                src_color -= channel;
                int diff = getDiffFactor(src_color, src_color + width * channel);

                float a = range_tbl[diff];
                *up_pass_factor-- = inv_alpha_f + a * (*prev_factor--);
                for (int c = 0; c < channel; c++)
                {
                    *up_pass_color-- = inv_alpha_f * (*src_color_hor--) + a * (*prev_color--);
                }
            }
        }
    });
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    dispatch_release(group);
#else
#pragma omp parallel for
    for( int par = 0; par < 2; par++ )
    {
        if( !par )
        {
            const float* src_color_hor = horiz_color;
            const uint16_t* src_color = img_src;
            float* down_pass_color = m_down_pass_color;
            float* down_pass_factor = m_down_pass_factor;

            const uint16_t* src_prev = src_color;
            const float* prev_color = down_pass_color;
            const float* prev_factor = down_pass_factor;

            for (int x = 0; x < width; x++)
            {
                *down_pass_factor++ = 1.f;
                for (int c = 0; c < channel; c++) *down_pass_color++ = *src_color_hor++;
                src_color += channel;
            }

            for (int y = 1; y < height; y++)
            {
                for (int x = 0; x < width-1; x++)
                {
                    int diff = getDiffFactor(src_color, src_prev);
                    src_prev += channel;
                    src_color += channel;

                    float a = range_tbl[diff];
                    *down_pass_factor++ = inv_alpha_f + a * (*prev_factor++);
                    for (int c = 0; c < channel; c++)
                    {
                        *down_pass_color++ = inv_alpha_f * (*src_color_hor++) + a * (*prev_color++);
                    }
                }
            }
        }
        else
        {
            int last_index = width * height * channel - 1;
            const uint16_t* src_color = img_src + last_index;
            const float* src_color_hor = horiz_color + last_index;
            float* up_pass_color = m_up_pass_color + last_index;
            float* up_pass_factor = m_up_pass_factor + (width * height - 1);

            const float* prev_color = up_pass_color;
            const float* prev_factor = up_pass_factor;

            for (int x = 0; x < width; x++)
            {
                *up_pass_factor-- = 1.f;
                for (int c = 0; c < channel; c++) *up_pass_color-- = *src_color_hor--;
                src_color -= channel;
            }

            for (int y = 1; y < height; y++)
            {
                for (int x = 0; x < width-1; x++)
                {
                    src_color -= channel;
                    int diff = getDiffFactor(src_color, src_color + width * channel);

                    float a = range_tbl[diff];
                    *up_pass_factor-- = inv_alpha_f + a * (*prev_factor--);
                    for (int c = 0; c < channel; c++)
                    {
                        *up_pass_color-- = inv_alpha_f * (*src_color_hor--) + a * (*prev_color--);
                    }
                }
            }
        }
    }
#endif

    // Final combine
#ifdef __APPLE__
    dispatch_apply(workers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t chunk) {
        int i_start = (int)((chunk * width_height) / workers);
        int i_end   = (int)(((chunk + 1) * width_height) / workers);
        for (int i = i_start; i < i_end; i++)
#else
#pragma omp parallel for
    for (int i = 0; i < width_height; i++)
#endif
        {
            float factor = 1.f / ((m_up_pass_factor[i]) + (m_down_pass_factor[i]));
            for (int c = 0; c < channel; c++)
            {
                int idx = i * channel + c;
                img_dst[idx] = (uint16_t)(factor * ((m_up_pass_color[idx]) + (m_down_pass_color[idx])));
            }
        }
#ifdef __APPLE__
    });
#endif
}
