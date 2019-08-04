#ifndef _MLVBlender_h_
#define _MLVBlender_h_

#include "MLVBlender_structs.h"

/* Crate a blender in some memory the size of a MLVBlender_t */
void init_MLVBlender(MLVBlender_t * Blender);
void free_MLVBlender(MLVBlender_t * Blender);

/* Generate and get output */
void MLVBlenderBlend(MLVBlender_t * Blender, uint64_t FrameIndex);
uint16_t * MLVBlenderGetOutput(MLVBlender_t * Blender);
int MLVBlenderGetOutputWidth(MLVBlender_t * Blender);
int MLVBlenderGetOutputHeight(MLVBlender_t * Blender);

/* Exports an mlv fil to OutputPath */
void MLVBlenderExportMLV(MLVBlender_t * Blender, const char * OutputPath);

/* Add an MLV */
void MLVBlenderAddMLV(MLVBlender_t * Blender, const char * MLVPath);

/* Get mlv file name */
const char * MLVBlenderGetMLVFileName(MLVBlender_t * Blender, int Index);

/* Get how many MLVs */
int MLVBlenderGetNumMLVs(MLVBlender_t * Blender);

/* Set/Get exposure */
void MLVBlenderSetMLVExposure(MLVBlender_t * Blender, int MLVIndex, float ExposureValue);
float MLVBlenderGetMLVExposure(MLVBlender_t * Blender, int MLVIndex);

/* Set offsets */
void MLVBlenderSetMLVOffsetX(MLVBlender_t * Blender, int MLVIndex, int Offset);
void MLVBlenderSetMLVOffsetY(MLVBlender_t * Blender, int MLVIndex, int Offset);
/* Get offsets */
int MLVBlenderGetMLVOffsetX(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVOffsetY(MLVBlender_t * Blender, int MLVIndex);

/* Set crops */
void MLVBlenderSetMLVCropLeft(MLVBlender_t * Blender, int MLVIndex, int Crop);
void MLVBlenderSetMLVCropRight(MLVBlender_t * Blender, int MLVIndex, int Crop);
void MLVBlenderSetMLVCropTop(MLVBlender_t * Blender, int MLVIndex, int Crop);
void MLVBlenderSetMLVCropBottom(MLVBlender_t * Blender, int MLVIndex, int Crop);
/* Get crops */
int MLVBlenderGetMLVCropLeft(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVCropRight(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVCropTop(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVCropBottom(MLVBlender_t * Blender, int MLVIndex);

/* Set feathering */
void MLVBlenderSetMLVFeatherLeft(MLVBlender_t * Blender, int MLVIndex, int Crop);
void MLVBlenderSetMLVFeatherRight(MLVBlender_t * Blender, int MLVIndex, int Crop);
void MLVBlenderSetMLVFeatherTop(MLVBlender_t * Blender, int MLVIndex, int Crop);
void MLVBlenderSetMLVFeatherBottom(MLVBlender_t * Blender, int MLVIndex, int Crop);
/* Get feathering */
int MLVBlenderGetMLVFeatherLeft(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVFeatherRight(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVFeatherTop(MLVBlender_t * Blender, int MLVIndex);
int MLVBlenderGetMLVFeatherBottom(MLVBlender_t * Blender, int MLVIndex);

/* Set visible */
void MLVBlenderSetMLVVisible(MLVBlender_t * Blender, int MLVIndex, int Visible);
int MLVBlenderGetMLVVisible(MLVBlender_t * Blender, int MLVIndex);

/* Difference blending */
void MLVBlenderSetMLVDifferenceBlending(MLVBlender_t * Blender, int MLVIndex, int UseDifference);
int MLVBlenderGetMLVDifferenceBlending(MLVBlender_t * Blender, int MLVIndex);

#endif