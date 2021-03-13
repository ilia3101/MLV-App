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
*/

#include "RBFilterPlain.h"
#include <algorithm>
#include <math.h>

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

	m_left_pass_color = new float[width_height_channel];
	m_left_pass_factor = new float[width_height];

	m_right_pass_color = new float[width_height_channel];
	m_right_pass_factor = new float[width_height];

	m_down_pass_color = new float[width_height_channel];
	m_down_pass_factor = new float[width_height];

	m_up_pass_color = new float[width_height_channel];
	m_up_pass_factor = new float[width_height];

#pragma omp parallel for
    for( int i = 0; i < width_height_channel; i++ )
    {
        m_left_pass_color[i] = 0;
        m_right_pass_color[i] = 0;
        m_down_pass_color[i] = 0;
        m_up_pass_color[i] = 0;
    }
#pragma omp parallel for
    for( int i = 0; i < width_height; i++ )
    {
        m_left_pass_factor[i] = 0;
        m_right_pass_factor[i] = 0;
        m_down_pass_factor[i] = 0;
        m_up_pass_factor[i] = 0;
    }
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
    //if(!(final_diff >= 0 && final_diff <= QX_DEF_U16_MAX))return 1;

	return final_diff;
}

// memory must be reserved before calling image filter
// this implementation of filter uses plain C++, single threaded
// channel count must be 3 or 4 (alpha not used)
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
    {
        float ii = 0.f;
#pragma omp parallel for
        for (int i = 0; i <= QX_DEF_U16_MAX; i++)
        {
            ii = -i;
            range_table_f[i] = alpha_f * exp(ii * inv_sigma_range);
        }
    }

#pragma omp parallel for
    for( int par = 0; par < 2; par++ )
    {
        ///////////////
        // Left pass
        if( !par )
        {
            const uint16_t* src_color = img_src;
            float* left_pass_color = m_left_pass_color;
            float* left_pass_factor = m_left_pass_factor;

            for (int y = 0; y < height-1; y++)
            {
                const uint16_t* src_prev = src_color;
                const float* prev_factor = left_pass_factor;
                const float* prev_color = left_pass_color;

                // process 1st pixel separately since it has no previous
                *left_pass_factor++ = 1.f;
                for (int c = 0; c < channel; c++)
                {
                    *left_pass_color++ = *src_color++;
                }

                // handle other pixels
                for (int x = 1; x < width; x++)
                {
                    // determine difference in pixel color between current and previous
                    // calculation is different depending on number of channels
                    int diff = getDiffFactor(src_color, src_prev);
                    src_prev = src_color;

                    float alpha_f = range_table_f[diff];

                    *left_pass_factor++ = inv_alpha_f + alpha_f * (*prev_factor++);

                    for (int c = 0; c < channel; c++)
                    {
                        *left_pass_color++ = inv_alpha_f * (*src_color++) + alpha_f * (*prev_color++);
                    }
                }
            }
        }
        else
        ///////////////
        // Right pass
        {
            // start from end and then go up to begining
            int last_index = width * height * channel - 1;
            const uint16_t* src_color = img_src + last_index;
            float* right_pass_color = m_right_pass_color + last_index;
            float* right_pass_factor = m_right_pass_factor + width * height - 1;

            for (int y = 0; y < height-1; y++)
            {
                //const uint16_t* src_prev = src_color;
                const float* prev_factor = right_pass_factor;
                const float* prev_color = right_pass_color;

                // process 1st pixel separately since it has no previous
                *right_pass_factor-- = 1.f;
                for (int c = 0; c < channel; c++)
                {
                    *right_pass_color-- = *src_color--;
                }

                // handle other pixels
                for (int x = 1; x < width; x++)
                {
                    // determine difference in pixel color between current and previous
                    // calculation is different depending on number of channels
                    int diff = getDiffFactor(src_color, src_color - 3);
                    //	src_prev = src_color;

                    float alpha_f = range_table_f[diff];

                    *right_pass_factor-- = inv_alpha_f + alpha_f * (*prev_factor--);

                    for (int c = 0; c < channel; c++)
                    {
                        *right_pass_color-- = inv_alpha_f * (*src_color--) + alpha_f * (*prev_color--);
                    }
                }
            }
        }
    }

    // vertical pass will be applied on top on horizontal pass, while using pixel differences from original image
    // result color stored in 'm_left_pass_color' and vertical pass will use it as source color
    {
        float* img_out = m_left_pass_color; // use as temporary buffer
        const float* left_pass_color = m_left_pass_color;
        const float* left_pass_factor = m_left_pass_factor;
        const float* right_pass_color = m_right_pass_color;
        const float* right_pass_factor = m_right_pass_factor;

        int width_height = width * height;
#pragma omp parallel for
        for (int i = 0; i < width_height; i++)
        {
            // average color divided by average factor
            float factor = 1.f / ((left_pass_factor[i]) + (right_pass_factor[i]));
            for (int c = 0; c < channel; c++)
            {
                int idx = i + i + i + c;
                img_out[idx] = (factor * ((left_pass_color[idx]) + (right_pass_color[idx])));
            }
        }
    }

#pragma omp parallel for
    for( int par = 0; par < 2; par++ )
    {
        ///////////////
        // Down pass
        if( !par )
        {
            const float* src_color_hor = m_left_pass_color; // result of horizontal pass filter

            const uint16_t* src_color = img_src;
            float* down_pass_color = m_down_pass_color;
            float* down_pass_factor = m_down_pass_factor;

            const uint16_t* src_prev = src_color;
            const float* prev_color = down_pass_color;
            const float* prev_factor = down_pass_factor;

            // 1st line done separately because no previous line
            for (int x = 0; x < width; x++)
            {
                *down_pass_factor++ = 1.f;
                for (int c = 0; c < channel; c++)
                {
                    *down_pass_color++ = *src_color_hor++;
                }
                src_color += channel;
            }

            // handle other lines
            for (int y = 1; y < height; y++)
            {
                for (int x = 0; x < width-1; x++)
                {
                    // determine difference in pixel color between current and previous
                    // calculation is different depending on number of channels
                    int diff = getDiffFactor(src_color, src_prev);
                    src_prev += channel;
                    src_color += channel;

                    float alpha_f = range_table_f[diff];

                    *down_pass_factor++ = inv_alpha_f + alpha_f * (*prev_factor++);

                    for (int c = 0; c < channel; c++)
                    {
                        *down_pass_color++ = inv_alpha_f * (*src_color_hor++) + alpha_f * (*prev_color++);
                    }
                }
            }
        }
        else
        ///////////////
        // Up pass
        {
            // start from end and then go up to begining
            int last_index = width * height * channel - 1;
            const uint16_t* src_color = img_src + last_index;
            const float* src_color_hor = m_left_pass_color + last_index; // result of horizontal pass filter
            float* up_pass_color = m_up_pass_color + last_index;
            float* up_pass_factor = m_up_pass_factor + (width * height - 1);

            //	const uint16_t* src_prev = src_color;
            const float* prev_color = up_pass_color;
            const float* prev_factor = up_pass_factor;

            // 1st line done separately because no previous line
            for (int x = 0; x < width; x++)
            {
                *up_pass_factor-- = 1.f;
                for (int c = 0; c < channel; c++)
                {
                    *up_pass_color-- = *src_color_hor--;
                }
                src_color -= channel;
            }

            // handle other lines
            for (int y = 1; y < height; y++)
            {
                for (int x = 0; x < width-1; x++)
                {
                    // determine difference in pixel color between current and previous
                    // calculation is different depending on number of channels
                    src_color -= channel;
                    int diff = getDiffFactor(src_color, src_color + width * channel);

                    float alpha_f = range_table_f[diff];

                    *up_pass_factor-- = inv_alpha_f + alpha_f * (*prev_factor--);

                    for (int c = 0; c < channel; c++)
                    {
                        *up_pass_color-- = inv_alpha_f * (*src_color_hor--) + alpha_f * (*prev_color--);
                    }
                }
            }
        }
    }

    ///////////////
    // average result of vertical pass is written to output buffer
    {
        const float* down_pass_color = m_down_pass_color;
        const float* down_pass_factor = m_down_pass_factor;
        const float* up_pass_color = m_up_pass_color;
        const float* up_pass_factor = m_up_pass_factor;

        int width_height = width * height;
#pragma omp parallel for
        for (int i = 0; i < width_height; i++)
        {
            // average color divided by average factor
            float factor = 1.f / ((up_pass_factor[i]) + (down_pass_factor[i]));
            for (int c = 0; c < channel; c++)
            {
                int idx = i + i + i + c;
                img_dst[idx] = (uint16_t)(factor * ((up_pass_color[idx]) + (down_pass_color[idx])));
            }
        }
    }
}
