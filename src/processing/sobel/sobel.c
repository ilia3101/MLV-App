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

#include "sobel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * Transforms the rgb information of an image stored in buffer to it's gray
 * representation
 */

int rgbToGray(uint16_t *rgb, uint16_t **gray, int buffer_size) {
    // Take size for gray image and allocate memory
    int gray_size = buffer_size / 3;
    *gray = malloc(sizeof(uint16_t) * gray_size);

    // Make pointers for iteration
    uint16_t *p_rgb = rgb;
    uint16_t *p_gray = *gray;

    // Calculate the value for every pixel in gray
    for(int i=0; i<gray_size; i++) {
        *p_gray = 0.30*p_rgb[0] + 0.59*p_rgb[1] + 0.11*p_rgb[2];

        p_rgb += 3;
        p_gray++;
    }

    return gray_size;
}

/*
 * Make the operation memory for iterative convolution
 */

void makeOpMem(uint16_t *buffer, int buffer_size, int width, int cindex, uint16_t *op_mem) {
    int bottom = cindex-width < 0;
    int top = cindex+width >= buffer_size;
    int left = cindex % width == 0;
    int right = (cindex+1) % width == 0;

    op_mem[0] = !bottom && !left  ? buffer[cindex-width-1] : 0;
    op_mem[1] = !bottom           ? buffer[cindex-width]   : 0;
    op_mem[2] = !bottom && !right ? buffer[cindex-width+1] : 0;

    op_mem[3] = !left             ? buffer[cindex-1]       : 0;
    op_mem[4] = buffer[cindex];
    op_mem[5] = !right            ? buffer[cindex+1]       : 0;

    op_mem[6] = !top && !left     ? buffer[cindex+width-1] : 0;
    op_mem[7] = !top              ? buffer[cindex+width]   : 0;
    op_mem[8] = !top && !right    ? buffer[cindex+width+1] : 0;
}

/*
 * Performs convolution between first two arguments
 */

int convolution(uint16_t *X, int *Y, int c_size) {
    int sum = 0;

    for(int i=0; i<c_size; i++) {
        sum += (int)(X[i]) * (int)(Y[c_size-i-1]);
    }

    if( sum < 0 ) sum = 0;
    if( sum > 65535 ) sum = 65535;

    return sum;
}

/*
 * Iterate Convolution
 */

void itConv(uint16_t *buffer, int buffer_size, int width, int *op, uint16_t **res) {
    // Allocate memory for result
    *res = malloc(sizeof(uint16_t) * buffer_size);

    // Temporary memory for each pixel operation
    uint16_t op_mem[SOBEL_OP_SIZE];
    memset(op_mem, 0, SOBEL_OP_SIZE);

    // Make convolution for every pixel
    for(int i=0; i<buffer_size; i++) {
        // Make op_mem
        makeOpMem(buffer, buffer_size, width, i, op_mem);

        // Convolution
        (*res)[i] = (uint16_t) abs(convolution(op_mem, op, SOBEL_OP_SIZE));

        /*
         * The abs function is used in here to avoid storing negative numbers
         * in a uint16_t data type array. It wouldn't make a different if the negative
         * value was to be stored because the next time it is used the value is
         * squared.
         */
    }
}

/*
 * Contour
 */

void contour(uint16_t *sobel_h, uint16_t *sobel_v, int gray_size, uint16_t **contour_img) {
    // Allocate memory for contour_img
    *contour_img = malloc(sizeof(uint16_t) * gray_size);

    // Iterate through every pixel to calculate the contour image
    for(int i=0; i<gray_size; i++) {
        int res = sqrt(pow(sobel_h[i], 2) + pow(sobel_v[i], 2));
        if( res > 65535 ) res = 65535;
        if( res < 0 ) res = 0;
        (*contour_img)[i] = (uint16_t) res;
    }
}

int sobelFilter(uint16_t *rgb, uint16_t **gray, uint16_t **sobel_h_res, uint16_t **sobel_v_res, uint16_t **contour_img, int width, int height) {
    int sobel_h[] = {-1, 0, 1, -2, 0, 2, -1, 0, 1},
        sobel_v[] = {1, 2, 1, 0, 0, 0, -1, -2, -1};

    int rgb_size = width*height*3;

    // Get gray representation of the image
    int gray_size = rgbToGray(rgb, gray, rgb_size);

    // Make sobel operations
    itConv(*gray, gray_size, width, sobel_h, sobel_h_res);
    itConv(*gray, gray_size, width, sobel_v, sobel_v_res);

    // Calculate contour matrix
    contour(*sobel_h_res, *sobel_v_res, gray_size, contour_img);

    return gray_size;
}

