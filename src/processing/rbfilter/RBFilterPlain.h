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

#pragma once

#include "stdint.h"

// This class is useful only for the sake of understanding the main principles of Recursive Bilateral Filter
// It is designed in non-optimal but easy to understand way. It also does not match 1:1 with original, 
// some creative liberties were taken with original idea.
// This class is not used in performance tests

class CRBFilterPlain
{
	int			m_reserve_width = 0;
	int			m_reserve_height = 0;
	int			m_reserve_channels = 0;

	float*		m_left_pass_color = nullptr;
	float*		m_left_pass_factor = nullptr;

	float*		m_right_pass_color = nullptr;
	float*		m_right_pass_factor = nullptr;

	float*		m_down_pass_color = nullptr;
	float*		m_down_pass_factor = nullptr;

	float*		m_up_pass_color = nullptr;
	float*		m_up_pass_factor = nullptr;

    int getDiffFactor(const uint16_t* color1, const uint16_t* color2) const;

public:

	CRBFilterPlain();
	~CRBFilterPlain();

	// assumes 3/4 channel images, 1 byte per channel
	void reserveMemory(int max_width, int max_height, int channels);
	void releaseMemory();

	// memory must be reserved before calling image filter
	// this implementation of filter uses plain C++, single threaded
	// channel count must be 3 or 4 (alpha not used)
    void filter(uint16_t* img_src, uint16_t* img_dst,
		float sigma_spatial, float sigma_range,
		int width, int height, int channel);
};
