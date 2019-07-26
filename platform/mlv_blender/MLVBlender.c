#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "MLVBlender.h"
#include "../../src/dng/dng.h"

#include <stdio.h>
void writebmp(unsigned char * data, int width, int height, char * filename) {
    int rowbytes = width*3+(4-(width*3%4))%4, imagesize = rowbytes*height, y;
    unsigned short header[] = {0x4D42,0,0,0,0,26,0,12,0,width,height,1,24};
    *((unsigned int *)(header+1)) = 26 + imagesize-((4-(width*3%4))%4);
    FILE * file = fopen(filename, "wb");
    fwrite(header, 1, 26, file);
    for (y = 0; y < height; ++y) fwrite(data+(y*width*3), rowbytes, 1, file);
    fwrite(data, width*3, 1, file);
    fclose(file);
}


static uint64_t file_set_pos(FILE *stream, uint64_t offset, int whence)
{
#if defined(__WIN32)
    return fseeko64(stream, offset, whence);
#else
    return fseek(stream, offset, whence);
#endif
}

void init_MLVBlender(MLVBlender_t * Blender)
{
    Blender->mode = 0;
    Blender->num_mlvs = 0;
    Blender->mlvs = malloc(sizeof(MLVBlender_mlv_t));
    Blender->blended_output = malloc(sizeof(uint16_t));

    return;
}

void free_MLVBlender(MLVBlender_t * Blender)
{
    for (int i = 0; i < Blender->num_mlvs; ++i)
    {
        MLVBlender_mlv_t * mlv = Blender->mlvs + i;
        freeMlvObject(mlv->mlv);
        free(mlv->file_name);
    }
    free(Blender->mlvs);

    free(Blender->blended_output);
}

void MLVBlenderAddMLV(MLVBlender_t * Blender, const char * MLVPath)
{
    Blender->mlvs = realloc(Blender->mlvs, (++Blender->num_mlvs) * sizeof(MLVBlender_mlv_t));

    MLVBlender_mlv_t * mlv = Blender->mlvs + Blender->num_mlvs-1;

    /* file name */
    mlv->file_name = malloc(strlen(MLVPath)+1);
    strcpy(mlv->file_name, MLVPath);

    /* offsets and crop */
    mlv->offset_x = 0;
    mlv->offset_y = 0;

    mlv->crop_left = 0;
    mlv->crop_right = 0;
    mlv->crop_bottom = 0;
    mlv->crop_top = 0;

    mlv->exposure = 1.0;

    /* mlv object */
    mlv->mlv = initMlvObjectWithClip(MLVPath, MLV_OPEN_FULL, NULL, NULL);
    disableMlvCaching(mlv->mlv);
    printMlvInfo(mlv->mlv);

    mlv->num_frames = getMlvFrames(mlv->mlv);
    mlv->width = getMlvWidth(mlv->mlv);
    mlv->height = getMlvHeight(mlv->mlv);

    // if (Blender->num_mlvs == 2) mlv->offset_y = -714, mlv->offset_x = 0;
}

const char * MLVBlenderGetMLVFileName(MLVBlender_t * Blender, int Index)
{
    return Blender->mlvs[Index].file_name;
}

int MLVBlenderGetNumMLVs(MLVBlender_t * Blender)
{
    return Blender->num_mlvs;
}

void MLVBlenderBlend(MLVBlender_t * Blender, uint64_t FrameIndex)
{
    /************************* CALCULATE RESULT SIZE **************************/

    int max_x = INT_MIN;
    int max_y = INT_MIN;
    int min_x = INT_MAX;
    int min_y = INT_MAX;

    for (int i = 0; i < Blender->num_mlvs; ++i)
    {
        MLVBlender_mlv_t * mlv = Blender->mlvs + i;

        /* Left edge */
        int x1 = mlv->offset_x + mlv->crop_left;
        /* Right edge */
        int x2 = mlv->offset_x + mlv->width - 1 - mlv->crop_right;
        /* Bottom edge */
        int y1 = mlv->offset_y + mlv->crop_bottom;
        /* Top edge */
        int y2 = mlv->offset_y + mlv->height - 1 - mlv->crop_top;

        if (x1 < min_x) min_x = x1;
        if (x2 > max_x) max_x = x2;
        if (y1 < min_y) min_y = y1;
        if (y2 > max_y) max_y = y2;
    }

    size_t output_width = max_x - min_x + 1;
    size_t output_height = max_y - min_y + 1;
    Blender->output_width = output_width;
    Blender->output_height = output_height;

    Blender->blended_output = realloc(Blender->blended_output, output_width * output_height * sizeof(float));

    /***************************** BEGIN BLENDING *****************************/

    for (int i = 0; i < Blender->num_mlvs; ++i)
    {
        MLVBlender_mlv_t * mlv = Blender->mlvs + i;
        size_t frame_size = mlv->width * mlv->height;
        uint16_t * frame_data = malloc(frame_size * sizeof(uint16_t));
        getMlvRawFrameUint16(mlv->mlv, FrameIndex, frame_data);

        int32_t black_level = getMlvBlackLevel(mlv->mlv);
        for (size_t j = 0; j < frame_size; ++j)
        {
            int32_t new_val = (int32_t)frame_data[j] - black_level;
            if (new_val < 0) new_val = 0;
            frame_data[j] = new_val;
        }

        float exposure = mlv->exposure;
        /* Put the pixels on the output */
        for (size_t y = mlv->crop_bottom; y < mlv->height-mlv->crop_top; ++y)
        {
            size_t index_src = mlv->width * y; /* Index for row Y */
            size_t index_dst = output_width * (y+mlv->offset_y-min_y) + mlv->offset_x-min_x; /* Index for row Y */

            for (size_t x = mlv->crop_left; x < mlv->width-mlv->crop_right; ++x)
            {
                Blender->blended_output[index_dst+x] = frame_data[index_src+x]*exposure;
            }
        }

        free(frame_data);
    }

    /* save to bmp */
    if (FrameIndex != 1) return;
    uint8_t * rgb = malloc(output_height*output_width*3);
    for (size_t i = 0; i < output_height*output_width; ++i)
    {
        rgb[i*3] = (int)(Blender->blended_output[i]/16);
        rgb[i*3+1] = Blender->blended_output[i]/16;
        rgb[i*3+2] = Blender->blended_output[i]/16;
    }
    writebmp(rgb, output_width, output_height, "output.bmp");
    free(rgb);
}

uint16_t * MLVBlenderGetOutput(MLVBlender_t * Blender)
{
    return Blender->blended_output;
}

int MLVBlenderGetOutputWidth(MLVBlender_t * Blender)
{
    return Blender->output_width;
}

int MLVBlenderGetOutputHeight(MLVBlender_t * Blender)
{
    return Blender->output_height;
}

void MLVBlenderExportMLV(MLVBlender_t * Blender, char * OutputPath)
{
    /* Limit length to shortest vid */
    uint64_t shortest_vid = Blender->mlvs[0].num_frames;
    for (int i = 0; i < Blender->num_mlvs; ++i)
        if (Blender->mlvs[0].num_frames < shortest_vid) shortest_vid = Blender->mlvs[0].num_frames;

    if (shortest_vid == 0) return;

    mlvObject_t * mlv_object = initMlvObjectWithClip(Blender->mlvs[0].file_name, MLV_OPEN_FULL, NULL, NULL);

    FILE * mlv_output_file = fopen(OutputPath, "wb");

    MLVBlenderBlend(Blender, 0);

    char error[256];

    /* Set fake dimensions inside MLV object and save headers */
    getMlvWidth(mlv_object) = MLVBlenderGetOutputWidth(Blender);
    getMlvHeight(mlv_object) = MLVBlenderGetOutputHeight(Blender);
    saveMlvHeaders(mlv_object, mlv_output_file, 0, MLV_COMPRESS, 0, shortest_vid, "420", error);
    // puts("hihio");

    size_t frame_size = MLVBlenderGetOutputWidth(Blender) * MLVBlenderGetOutputHeight(Blender);
    uint16_t * buffer16 = malloc(sizeof(uint16_t) * frame_size);
    uint8_t * buffer_compressed = malloc(2 * frame_size * sizeof(uint16_t));

    for (uint64_t f = 0; f < shortest_vid; ++f)
    {
        if (f != 0) MLVBlenderBlend(Blender, f);

        float black_level = getMlvBlackLevel(mlv_object);
        float maximum = pow(2.0, mlv_object->RAWI.raw_info.bits_per_pixel);
        for (size_t i = 0; i < frame_size; ++i) {
            float result = Blender->blended_output[i] + black_level;
            if (result > maximum) result = maximum;
            if (result < 0) result = 0;
            buffer16[i] = (uint16_t)result;
        }

        size_t frame_size_compressed = 0;
        int ret = dng_compress_image(buffer_compressed, buffer16, &frame_size_compressed, mlv_object->RAWI.xRes, mlv_object->RAWI.yRes, mlv_object->RAWI.raw_info.bits_per_pixel);

        /* Write frame */
        mlv_vidf_hdr_t vidf_hdr = { 0 };
        int chunk = mlv_object->video_index[f].chunk_num;
        uint64_t block_offset = mlv_object->video_index[f].block_offset;
        /* read VIDF block header */
        file_set_pos(mlv_object->file[chunk], block_offset, SEEK_SET);
        fread(&vidf_hdr, sizeof(mlv_vidf_hdr_t), 1, mlv_object->file[chunk]);
        vidf_hdr.frameSpace = 0;
        vidf_hdr.blockSize = sizeof(mlv_vidf_hdr_t) + frame_size_compressed;
        fwrite(&vidf_hdr, sizeof(mlv_vidf_hdr_t), 1, mlv_output_file);
        fwrite(buffer_compressed, frame_size_compressed, 1, mlv_output_file);
    }

    free(buffer16);
    free(buffer_compressed);

    getMlvWidth(mlv_object) = Blender->mlvs[0].width;
    getMlvHeight(mlv_object) = Blender->mlvs[0].height;

    freeMlvObject(mlv_object);

    fclose(mlv_output_file);

    puts("yes");
    return;
}

void MLVBlenderSetMLVExposure(MLVBlender_t * Blender, int MLVIndex, float ExposureValue)
{
    Blender->mlvs[MLVIndex].exposure = ExposureValue;
}
float MLVBlenderGetMLVExposure(MLVBlender_t * Blender, int MLVIndex)
{
    return Blender->mlvs[MLVIndex].exposure;
}

/* Set offsets */
void MLVBlenderSetMLVOffsetX(MLVBlender_t * Blender, int MLVIndex, int Offset) {
    Blender->mlvs[MLVIndex].offset_x = Offset;
} void MLVBlenderSetMLVOffsetY(MLVBlender_t * Blender, int MLVIndex, int Offset) {
    Blender->mlvs[MLVIndex].offset_y = Offset;
}
/* Get offsets */
int MLVBlenderGetMLVOffsetX(MLVBlender_t * Blender, int MLVIndex) {
    return Blender->mlvs[MLVIndex].offset_x;
} int MLVBlenderGetMLVOffsetY(MLVBlender_t * Blender, int MLVIndex) {
    return Blender->mlvs[MLVIndex].offset_y;
}

/* Set crops */
void MLVBlenderSetMLVCropRight(MLVBlender_t * Blender, int MLVIndex, int Crop) {
    Blender->mlvs[MLVIndex].crop_right = Crop;
} void MLVBlenderSetMLVCropLeft(MLVBlender_t * Blender, int MLVIndex, int Crop) {
    Blender->mlvs[MLVIndex].crop_left = Crop;
} void MLVBlenderSetMLVCropTop(MLVBlender_t * Blender, int MLVIndex, int Crop) {
    Blender->mlvs[MLVIndex].crop_top = Crop;
} void MLVBlenderSetMLVCropBottom(MLVBlender_t * Blender, int MLVIndex, int Crop) {
    Blender->mlvs[MLVIndex].crop_bottom = Crop;
}
/* Get crops */
int MLVBlenderGetMLVCropRight(MLVBlender_t * Blender, int MLVIndex) {
    return Blender->mlvs[MLVIndex].crop_right;
} int MLVBlenderGetMLVCropLeft(MLVBlender_t * Blender, int MLVIndex) {
    return Blender->mlvs[MLVIndex].crop_left;
} int MLVBlenderGetMLVCropTop(MLVBlender_t * Blender, int MLVIndex) {
    return Blender->mlvs[MLVIndex].crop_top;
} int MLVBlenderGetMLVCropBottom(MLVBlender_t * Blender, int MLVIndex) {
    return Blender->mlvs[MLVIndex].crop_bottom;
}