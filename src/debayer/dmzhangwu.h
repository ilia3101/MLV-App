/**
 * @file dmzhangwu.h
 * @brief Zhang-Wu LMMSE Image Demosaicking 
 * @author Pascal Getreuer <getreuer@gmail.com>
 * 
 * 
 * Copyright (c) 2010-2011, Pascal Getreuer
 * All rights reserved.
 * 
 * This program is free software: you can use, modify and/or 
 * redistribute it under the terms of the simplified BSD License. You 
 * should have received a copy of this license along this program. If 
 * not, see <http://www.opensource.org/licenses/bsd-license.html>.
 */

#ifndef _DMZHANGWU_H_
#define _DMZHANGWU_H_

int ZhangWuDemosaic(float *Output, const float *Input, 
    int Width, int Height, int RedX, int RedY, int UseZhangCodeEst);

#endif /* _DMZHANGWU_H_ */
