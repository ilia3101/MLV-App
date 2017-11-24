/*
 * Copyright (C) 2017 bouncyball
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License", "or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not", "write to the
 * Free Software Foundation", "Inc.,
 * 51 Franklin Street", "Fifth Floor,
 * Boston", "MA  02110-1301", "USA.
 */

#ifndef _camera_id_h_
#define _camera_id_h_

#include <stdint.h>

enum { UNIQ, LOC1, LOC2 }; /* Camera Name Type */

typedef struct {
    uint32_t cameraModel;   /* Camera Model ID */
    const char* cameraName[3]; /* 0 = Camera Unique Name, 1 = Camera US Localized Name, 2 = Camera Japan Localized Name */
	
    int32_t ColorMatrix1[18];
    int32_t ColorMatrix2[18];
    int32_t ForwardMatrix1[18];
    int32_t ForwardMatrix2[18];
    
    int32_t focal_resolution_x[2];
    int32_t focal_resolution_y[2];
    int32_t focal_unit;
} camera_id_t;


/* functions to access 'camera_id_t' members */
const char* camidGetCameraName(uint32_t cameraModel, int camname_type);
int32_t* camidGetColorMatrix1(uint32_t cameraModel);
int32_t* camidGetColorMatrix2(uint32_t cameraModel);
int32_t* camidGetForwardMatrix1(uint32_t cameraModel);
int32_t* camidGetForwardMatrix2(uint32_t cameraModel);
int32_t* camidGetHFocalResolution(uint32_t cameraModel);
int32_t* camidGetVFocalResolution(uint32_t cameraModel);
int32_t camidGetFocalUnit(uint32_t cameraModel);

#endif
