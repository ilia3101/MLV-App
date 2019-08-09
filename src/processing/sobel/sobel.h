/*
 * Copyright 2018 Pedro Melgueira
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SOBEL_H
#define SOBEL_H

#define SOBEL_OP_SIZE 9

#include <stdint.h>

int  rgbToGray   (uint16_t *rgb, uint16_t **gray, int buffer_size);
void makeOpMem   (uint16_t *buffer, int buffer_size, int width, int cindex, uint16_t *op_mem);
int  convolution (uint16_t *X, int *Y, int c_size);
void itConv      (uint16_t *buffer, int buffer_size, int width, int *op, uint16_t **res);
void contour     (uint16_t *sobel_h, uint16_t *sobel_v, int gray_size, uint16_t **contour_img);
int  sobelFilter (uint16_t *rgb, uint16_t **gray, uint16_t **sobel_h_res, uint16_t **sobel_v_res, uint16_t **contour_img, int width, int height);

#endif

