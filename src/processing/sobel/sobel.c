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

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

/*
 * Transforms the rgb information of an image stored in buffer to it's gray
 * representation
 */

int rgbToGray(uint16_t *rgb, uint16_t **gray, int buffer_size) {
    // Take size for gray image and allocate memory
    int gray_size = buffer_size / 3;
    *gray = malloc(sizeof(uint16_t) * gray_size);

#ifdef __APPLE__
    size_t nworkers = (gray_size < 8) ? (size_t)gray_size : 8;
    if (nworkers < 1) nworkers = 1;
    size_t chunk = ((size_t)gray_size + nworkers - 1) / nworkers;
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        int i_start = (int)(t * chunk);
        int i_end = (int)(i_start + chunk);
        if (i_end > gray_size) i_end = gray_size;
        uint16_t *p_rgb = rgb + i_start * 3;
        uint16_t *p_gray = *gray + i_start;
        for (int i = i_start; i < i_end; i++) {
            *p_gray = 0.30*p_rgb[0] + 0.59*p_rgb[1] + 0.11*p_rgb[2];
            p_rgb += 3;
            p_gray++;
        }
    });
#else
#pragma omp parallel for schedule(static)
    for(int i=0; i<gray_size; i++) {
        uint16_t *p_rgb = rgb + i * 3;
        (*gray)[i] = 0.30*p_rgb[0] + 0.59*p_rgb[1] + 0.11*p_rgb[2];
    }
#endif

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

    // Make convolution for every pixel with thread-private op_mem
#ifdef __APPLE__
    size_t nworkers = (buffer_size < 8) ? (size_t)buffer_size : 8;
    if (nworkers < 1) nworkers = 1;
    size_t chunk = ((size_t)buffer_size + nworkers - 1) / nworkers;
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        int i_start = (int)(t * chunk);
        int i_end = (int)(i_start + chunk);
        if (i_end > buffer_size) i_end = buffer_size;
        uint16_t op_mem[SOBEL_OP_SIZE];
        for (int i = i_start; i < i_end; i++) {
            makeOpMem(buffer, buffer_size, width, i, op_mem);
            (*res)[i] = (uint16_t) abs(convolution(op_mem, op, SOBEL_OP_SIZE));
        }
    });
#else
#pragma omp parallel for schedule(static)
    for(int i=0; i<buffer_size; i++) {
        uint16_t op_mem[SOBEL_OP_SIZE];
        makeOpMem(buffer, buffer_size, width, i, op_mem);
        (*res)[i] = (uint16_t) abs(convolution(op_mem, op, SOBEL_OP_SIZE));
    }
#endif
}

/*
 * Contour
 */

void contour(uint16_t *sobel_h, uint16_t *sobel_v, int gray_size, uint16_t **contour_img) {
    // Allocate memory for contour_img
    *contour_img = malloc(sizeof(uint16_t) * gray_size);

    // Iterate through every pixel to calculate the contour image with exact double math
#ifdef __APPLE__
    size_t nworkers = (gray_size < 8) ? (size_t)gray_size : 8;
    if (nworkers < 1) nworkers = 1;
    size_t chunk = ((size_t)gray_size + nworkers - 1) / nworkers;
    dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
        int i_start = (int)(t * chunk);
        int i_end = (int)(i_start + chunk);
        if (i_end > gray_size) i_end = gray_size;
        for (int i = i_start; i < i_end; i++) {
            double gh = (double)sobel_h[i];
            double gv = (double)sobel_v[i];
            int res = (int)sqrt(gh * gh + gv * gv);
            if( res > 65535 ) res = 65535;
            if( res < 0 ) res = 0;
            (*contour_img)[i] = (uint16_t) res;
        }
    });
#else
#pragma omp parallel for schedule(static)
    for(int i=0; i<gray_size; i++) {
        double gh = (double)sobel_h[i];
        double gv = (double)sobel_v[i];
        int res = (int)sqrt(gh * gh + gv * gv);
        if( res > 65535 ) res = 65535;
        if( res < 0 ) res = 0;
        (*contour_img)[i] = (uint16_t) res;
    }
#endif
}

static const int sobel_h_table[] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
static const int sobel_v_table[] = {1, 2, 1, 0, 0, 0, -1, -2, -1};

int sobelFilter(uint16_t *rgb, uint16_t **gray, uint16_t **sobel_h_res, uint16_t **sobel_v_res, uint16_t **contour_img, int width, int height) {
    int rgb_size = width*height*3;
    uint16_t *local_gray = NULL;

    // Get gray representation of the image
    int gray_size = rgbToGray(rgb, (gray ? gray : &local_gray), rgb_size);
    uint16_t *g_buf = gray ? *gray : local_gray;

    // If caller does not need intermediate sobel_h_res and sobel_v_res buffers, fuse directly into contour_img
    if (sobel_h_res == NULL && sobel_v_res == NULL && contour_img != NULL) {
        *contour_img = malloc(sizeof(uint16_t) * gray_size);
#ifdef __APPLE__
        size_t nworkers = (gray_size < 8) ? (size_t)gray_size : 8;
        if (nworkers < 1) nworkers = 1;
        size_t chunk = ((size_t)gray_size + nworkers - 1) / nworkers;
        const int *h_op = sobel_h_table;
        const int *v_op = sobel_v_table;
        dispatch_apply(nworkers, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t t) {
            int i_start = (int)(t * chunk);
            int i_end = (int)(i_start + chunk);
            if (i_end > gray_size) i_end = gray_size;
            uint16_t op_mem[SOBEL_OP_SIZE];
            for (int i = i_start; i < i_end; ++i) {
                makeOpMem(g_buf, gray_size, width, i, op_mem);
                int sh = abs(convolution(op_mem, (int*)h_op, SOBEL_OP_SIZE));
                int sv = abs(convolution(op_mem, (int*)v_op, SOBEL_OP_SIZE));
                double gh = (double)sh;
                double gv = (double)sv;
                int res = (int)sqrt(gh * gh + gv * gv);
                if (res > 65535) res = 65535;
                if (res < 0) res = 0;
                (*contour_img)[i] = (uint16_t)res;
            }
        });
#else
#pragma omp parallel for schedule(static)
        for (int i = 0; i < gray_size; ++i) {
            uint16_t op_mem[SOBEL_OP_SIZE];
            makeOpMem(g_buf, gray_size, width, i, op_mem);
            int sh = abs(convolution(op_mem, (int*)sobel_h_table, SOBEL_OP_SIZE));
            int sv = abs(convolution(op_mem, (int*)sobel_v_table, SOBEL_OP_SIZE));
            double gh = (double)sh;
            double gv = (double)sv;
            int res = (int)sqrt(gh * gh + gv * gv);
            if (res > 65535) res = 65535;
            if (res < 0) res = 0;
            (*contour_img)[i] = (uint16_t)res;
        }
#endif
    } else {
        // Standard path for callers requesting intermediate buffers
        if (sobel_h_res) itConv(g_buf, gray_size, width, (int*)sobel_h_table, sobel_h_res);
        if (sobel_v_res) itConv(g_buf, gray_size, width, (int*)sobel_v_table, sobel_v_res);
        if (contour_img && sobel_h_res && sobel_v_res) contour(*sobel_h_res, *sobel_v_res, gray_size, contour_img);
    }

    if (!gray && local_gray) free(local_gray);

    return gray_size;
}

