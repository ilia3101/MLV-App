#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "video_mlv.h"
#include "raw.h"
#include "mlv.h"

/* Debayering module */
#include "../debayer/debayer.h"
/* Processing module */
#include "../processing/raw_processing.h"

/* Unpacks the bits of a frame to get a bayer B&W image (without black level correction)
 * Needs memory to return to, sized: sizeof(float) * getMlvHeight(urvid) * getMlvWidth(urvid)
 * Output image's pixels will be in range 0-65535 as if it is 16 bit integers */
void getMlvRawFrameFloat(mlvObject_t * video, int frameIndex, float * outputFrame)
{
    int bitdepth = video->RAWI.raw_info.bits_per_pixel;
    int width = video->RAWI.xRes;
    int height = video->RAWI.yRes;

    /* How many bytes is RAW frame */
    int raw_frame_size = (width * height * bitdepth) / 8;

    /* Memory for RAW data */
    uint8_t * RAWFrame = (uint8_t *)malloc( raw_frame_size );

    /* Move to start of frame in file and read the RAW data */
    fseek(video->file, video->frame_offsets[frameIndex], SEEK_SET);
    fread(RAWFrame, sizeof(uint8_t), raw_frame_size, video->file);

    switch (bitdepth)
    {
        case 14:
            /* 8 pixels every 14 bytes */
            for (int raw_byte = 0; raw_byte < raw_frame_size; raw_byte += 14)
            {
                /* Use the raw_pixblock struct to split up the bytes into parts of pixels */
                raw_pixblock_14 * pixel = (raw_pixblock_14 *)(RAWFrame + raw_byte);

                /* Location in output image */
                float * out_pixel = outputFrame + ((raw_byte/14) * 8);

                /* Join the pieces of pixels a-h from raw_pixblock and left shift 2 to get 16 bit values */
                 * out_pixel = (float) ( (pixel->a) << 2 );
                out_pixel[1] = (float) ( (pixel->b_lo | (pixel->b_hi << 12)) << 2 );
                out_pixel[2] = (float) ( (pixel->c_lo | (pixel->c_hi << 10)) << 2 );
                out_pixel[3] = (float) ( (pixel->d_lo | (pixel->d_hi <<  8)) << 2 );
                out_pixel[4] = (float) ( (pixel->e_lo | (pixel->e_hi <<  6)) << 2 );
                out_pixel[5] = (float) ( (pixel->f_lo | (pixel->f_hi <<  4)) << 2 );
                out_pixel[6] = (float) ( (pixel->g_lo | (pixel->g_hi <<  2)) << 2 );
                out_pixel[7] = (float) ( (pixel->h) << 2 );
            }

            break;

        case 12:
            /* 8 pixels every 12 bytes */
            for (int raw_byte = 0; raw_byte < raw_frame_size; raw_byte += 12)
            {
                /* Use the raw_pixblock struct to split up the bytes into parts of pixels */
                raw_pixblock_12 * pixel = (raw_pixblock_12 *)(RAWFrame + raw_byte);

                /* Location in output image */
                float * out_pixel = outputFrame + ((raw_byte/12) * 8);

                /* Join the pieces of pixels a-h from raw_pixblock and left shift 2 to get 16 bit values */
                 * out_pixel = (float) ( (pixel->a) << 4 );
                out_pixel[1] = (float) ( (pixel->b_lo | (pixel->b_hi << 8)) << 4 );
                out_pixel[2] = (float) ( (pixel->c_lo | (pixel->c_hi << 4)) << 4 );
                out_pixel[3] = (float) ( (pixel->d) << 4 );
                out_pixel[4] = (float) ( (pixel->e) << 4 );
                out_pixel[5] = (float) ( (pixel->f_lo | (pixel->f_hi << 8)) << 4 );
                out_pixel[6] = (float) ( (pixel->g_lo | (pixel->g_hi << 4)) << 4 );
                out_pixel[7] = (float) ( (pixel->h) << 4 );
            }

            break;

        case 10:
            /* 8 pixels every 10 bytes */
            for (int raw_byte = 0; raw_byte < raw_frame_size; raw_byte += 10)
            {
                /* Use the raw_pixblock struct to split up the bytes into parts of pixels */
                raw_pixblock_10 * pixel = (raw_pixblock_10 *)(RAWFrame + raw_byte);

                /* Location in output image */
                float * out_pixel = outputFrame + ((raw_byte/10) * 8);

                /* Join the pieces of pixels a-h from raw_pixblock and left shift 2 to get 16 bit values */
                 * out_pixel = (float) ( (pixel->a) << 6 );
                out_pixel[1] = (float) ( (pixel->b_lo | (pixel->b_hi << 4)) << 6 );
                out_pixel[2] = (float) ( (pixel->c) << 6 );
                out_pixel[3] = (float) ( (pixel->d_lo | (pixel->d_hi << 8)) << 6 );
                out_pixel[4] = (float) ( (pixel->e_lo | (pixel->e_hi << 2)) << 6 );
                out_pixel[5] = (float) ( (pixel->f) << 6 );
                out_pixel[6] = (float) ( (pixel->g_lo | (pixel->g_hi << 6)) << 6 );
                out_pixel[7] = (float) ( (pixel->h) << 6 );
            }

            break;

        /* Placeholder coming up */

        case 69:
            /* Do losssless decompression stuff */
            break;
    }
}

void setMlvProcessing(mlvObject_t * video, processingObject_t * processing)
{
    double camera_matrix[9];
    double xyz_to_rgb_matrix[9];

    /* Easy bit */
    video->processing = processing;

    /* MATRIX stuff (not working, so commented out - 
     * processing object defaults to 1,0,0,0,1,0,0,0,1) */

    /* Get camera matrix for MLV clip and set it in the processing object */
    // getMlvXyzToCameraMatrix(video, camera_matrix);
    /* Set Camera to XYZ */
    // processingSetXyzToCamMatrix(processing, camera_matrix);

    /* Get and set some XYZ to RGB matrix(a funky one) */
    // getMlvNiceXyzToRgbMatrix(video, xyz_to_rgb_matrix);
    // processingSetXyzToRgbMatrix(processing, xyz_to_rgb_matrix);

    /* BLACK / WHITE level */
    processingSetBlackAndWhiteLevel( processing, 
                                     getMlvBlackLevel(video) * 4,
                                     getMlvWhiteLevel(video) * 4 );

    /* DONE? */
}

void getMlvRawFrameDebayered(mlvObject_t * video, int frameIndex, uint16_t * outputFrame)
{
    int width = getMlvWidth(video);
    int height = getMlvHeight(video);
    int frame_size = width * height * sizeof(uint16_t) * 3;

    /* If frame is cached just giv it */
    if (video->cached_frames[frameIndex])
    {
        memcpy(outputFrame, video->rgb_raw_frames[frameIndex], frame_size);
    }
    /* Else do debayering etc */
    else
    {
        float * raw_frame = malloc(width * height * sizeof(float));
        get_mlv_raw_frame_debayered(video, frameIndex, raw_frame, outputFrame, 0);
        free(raw_frame);
    }
}

/* Get a processed frame in 8 bit */
void getMlvProcessedFrame8(mlvObject_t * video, int frameIndex, uint8_t * outputFrame)
{
    /* Useful */
    int width = getMlvWidth(video);
    int height = getMlvHeight(video);

    /* How many bytes is RAW frame */
    int raw_frame_size = width * height;
    int rgb_frame_size = raw_frame_size * 3;

    /* Unprocessed debayered frame (RGB) */
    uint16_t * unprocessed_frame = malloc( rgb_frame_size * sizeof(uint16_t) );
    /* Processed frame (RGB) */
    uint16_t * processed_frame = malloc( rgb_frame_size * sizeof(uint16_t) );

    /* Get the raw data in B&W */
    getMlvRawFrameDebayered(video, frameIndex, unprocessed_frame);

    /* Do processing.......... */
    applyProcessingObject( video->processing, 
                           width, height,
                           unprocessed_frame,
                           processed_frame );

    /* Copy (and 8-bitize) */
    for (int i = 0; i < rgb_frame_size; ++i)
    {
        outputFrame[i] = processed_frame[i] >> 8;
    }

    free(unprocessed_frame);
    free(processed_frame);
}

/* To initialise mlv object with a clip 
 * Three functions in one */
mlvObject_t * initMlvObjectWithClip(char * mlvPath)
{
    mlvObject_t * video = initMlvObject();
    openMlvClip(video, mlvPath);
    mapMlvFrames(video, 0);
    return video;
}

/* Allocates a tiny bit of memory for everything in the structure
 * so we can always be sure there is memory, and when we need to 
 * resize it, simply do free followed by malloc */
mlvObject_t * initMlvObject()
{
    mlvObject_t * video = (mlvObject_t *)calloc( 1, sizeof(mlvObject_t) );
    /* Just 1 element for now */
    video->frame_offsets = (uint32_t *)malloc( sizeof(uint32_t) );

    /* Cache things, only one element for now as it is empty */
    video->rgb_raw_frames = (uint16_t **)malloc( sizeof(uint16_t *) );
    video->cached_frames = (uint8_t *)malloc( sizeof(uint8_t) );
    /* All frames in one block of memory for least mallocing during usage */
    video->cache_memory_block = (uint16_t *)malloc( sizeof(uint16_t) );

    /* Set cache limit to allow ~1 second of 1080p and be safe for low ram PCs */
    setMlvRawCacheLimitMegaBytes(video, 290);
    setMlvCacheStartFrame(video, 0); /* Just in case */

    /* Seems about right */
    setMlvCpuCores(video, 4);

    /* Retun pointer */
    return video;
}

/* Free all memory and close file */
void freeMlvObject(mlvObject_t * video)
{
    /* Close MLV file */
    fclose(video->file);
    /* Free all memory */
    free(video->frame_offsets);

    /*** Free cache stuff ***/

    /* make sure its stopped using silly trick */
    video->stop_caching = 1;
    while (video->is_caching) usleep(100);

    /* Now free these */
    free(video->cached_frames);
    free(video->rgb_raw_frames);
    free(video->cache_memory_block);

    /* Main 1 */
    free(video);
}

/* openMlvClip() and mapMlvFrames() should be combined */

/* Reads an MLV file in to a mlv object(mlvObject_t struct) 
 * only puts metadata in to the mlvObject_t, 
 * no debayering or bit unpacking */
void openMlvClip(mlvObject_t * video, char * mlvPath)
{
    video->file = (FILE *)fopen(mlvPath, "rb");

    /* Getting size of file in bytes */
    fseek(video->file, 0, SEEK_END);
    uint32_t file_size = ftell(video->file);

    char block_name[4]; /* Read header name to this */
    uint32_t block_size; /* Size of block */
    uint32_t block_num = 0; /* Number of blocks in file */
    uint32_t frame_total = 0; /* Number of frames in video */

    fseek(video->file, 0, SEEK_SET); /* Start of file */

    while (ftell(video->file) < file_size) /* Check if were at end of file yet */
    {
        /* Record position to go back to it later if block is read */
        uint32_t block_start = ftell(video->file);
        /* Read block name */
        fread(&block_name, sizeof(char), 4, video->file);
        /* Read size of block to block_size variable */
        fread(&block_size, sizeof(uint32_t), 1, video->file);
        /* Next block location */
        uint32_t next_block = block_start + block_size;

        /* Go back to start of block for next bit */
        fseek(video->file, block_start, SEEK_SET);

        /* Now check what kind of block it is and read it in to the mlv object */

        /* Is a frame block */
        if ( strncmp(block_name, "VIDF", 4) == 0 ) 
        {   
            /* Read block info to VIDF part(only once) */
            if (frame_total < 1) fread(&video->VIDF, sizeof(mlv_vidf_hdr_t), 1, video->file);
            /* Keep track of number of frames */
            frame_total++;
        }
        /* Nowhere did it say that the "MLVI" block == mlv_file_hdr_t / "FILE" */
        else if ( strncmp(block_name, "MLVI", 4) == 0 || strncmp(block_name, "FILE", 4) == 0 )
            fread(&video->MLVI, sizeof(mlv_file_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "AUDF", 4) == 0 )
            fread(&video->AUDF, sizeof(mlv_audf_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "RAWI", 4) == 0 )
            fread(&video->RAWI, sizeof(mlv_rawi_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "WAVI", 4) == 0 )
            fread(&video->WAVI, sizeof(mlv_wavi_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "EXPO", 4) == 0 )
            fread(&video->EXPO, sizeof(mlv_expo_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "LENS", 4) == 0 )
            fread(&video->LENS, sizeof(mlv_lens_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "RTCI", 4) == 0 )
            fread(&video->RTCI, sizeof(mlv_rtci_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "IDNT", 4) == 0 )
            fread(&video->IDNT, sizeof(mlv_idnt_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "INFO", 4) == 0 )
            fread(&video->INFO, sizeof(mlv_info_hdr_t), 1, video->file);
        else if ( strncmp(block_name, "DISO", 4) == 0 )
            fread(&video->DISO, sizeof(mlv_diso_hdr_t), 1, video->file);

        /* Printing stuff for fun */
        printf("Block #%4i  |  %.4s  |%9i Bytes\n", block_num, block_name, block_size);

        /* Move to next block */
        fseek(video->file, next_block, SEEK_SET);

        block_num++;
    }

    /* We work in an imaginary 14 bit world, so if its 10/12 bit, blackwhite levels shall be multiplied */
    if (getMlvBitdepth(video) == 12)
    {
        /* We can be cheeky with the macros like this (maybe they could be renamed without 'get') */
        getMlvBlackLevel(video) *= 4;
        getMlvWhiteLevel(video) *= 4;
    }
    else if (getMlvBitdepth(video) == 10)
    {
        getMlvBlackLevel(video) *= 16;
        getMlvWhiteLevel(video) *= 16;
    }

    /* let's be kind and repair black level if it's broken (OMG IT WORKS!) */
    if ((getMlvBlackLevel(video) < 1700) || (getMlvBlackLevel(video) > 2200))
    {
        int old_black_level = getMlvBlackLevel(video);

        /* Camera specific stuff */
        if ( (getMlvCamera(video)[11] == 'D' && getMlvCamera(video)[20] != 'I') ||
             (getMlvCamera(video)[10] == '5' && getMlvCamera(video)[12] == 'D') )
        {
            /* 5D2 and 50D black level */
            getMlvBlackLevel(video) = 1792;
        }
        else
        {
            /* All other camz */
            getMlvBlackLevel(video) = 2048;
        }

        /* User needs to know how amazingly kind we are */
        printf("\nPSA!!!\nYour black level was wrong!\n");
        printf("It was: %i, and has been changed to: %i, which should be right for your %s\n\n", 
            old_black_level, getMlvBlackLevel(video), getMlvCamera(video));
    }

    video->block_num = block_num;

    /* Set frame count in video object */
    video->frames = frame_total;
    /* Calculate framerate */
    video->frame_rate = (double)video->MLVI.sourceFpsNom / (double)video->MLVI.sourceFpsDenom;

    /* Make sure frame cache number is up to date by rerunning thiz */
    setMlvRawCacheLimitMegaBytes(video, getMlvRawCacheLimitMegaBytes(video));

    /* For frame cache */
    free(video->rgb_raw_frames);
    free(video->cached_frames);
    video->rgb_raw_frames = (uint16_t **)malloc( sizeof(uint16_t *) * frame_total );
    video->cached_frames = (uint8_t *)calloc( sizeof(uint8_t), frame_total );
}


/* mapMlvFrames function will get byte offsets of every frame in the file, run this
 * after mlvObject_t is initialised and video is opened, or you won't have frames */
void mapMlvFrames(mlvObject_t * video, int limit)
{
    /* Getting size of file in bytes */
    fseek(video->file, 0, SEEK_END); /* Go to end */
    int file_size = ftell(video->file); /* Get positions */

    char block_name[4]; /* Read header name to this */
    uint32_t block_size; /* Size of block */
    uint32_t frame_offset; /* Offset to (a)frame from start of VIDF block */
    uint32_t frame_num = 0; /* Number of frames in video */
    uint32_t frame_total = 0; /* Number of frames in video */

    fseek(video->file, 0, SEEK_SET); /* Start of file */

    /* Memory 4 all frame offsets */
    free(video->frame_offsets);
    video->frame_offsets = (uint32_t *)malloc( (video->frames) * sizeof(uint32_t) );

    while (ftell(video->file) < file_size) /* Check if end of file yet */
    {
        /* Record position to go back to it later when block is read */
        uint32_t block_start = ftell(video->file);
        /* Read block name */
        fread(&block_name, sizeof(char), 4, video->file);
        /* Read size of block to block_size variable */
        fread(&block_size, sizeof(uint32_t), 1, video->file);
        /* Next block location */
        uint32_t next_block = block_start + block_size;
        
        /* Is it frame block? */
        if ( strncmp(block_name, "VIDF", 4) == 0 )
        {
            fseek(video->file, 8, SEEK_CUR); /* skip 8 bytes */

            /* I've heard MLV frames can be out of order... 
             * So check its number... */
            fread(&frame_num, sizeof(uint32_t), 1, video->file);

            fseek(video->file, 8, SEEK_CUR); /* skip 8 bytes */

            /* Get frame offset from current location */
            fread(&frame_offset, sizeof(uint32_t), 1, video->file);

            printf("frame %i/%i, %iMB / %i Bytes from start of file\n",
            frame_num, video->frames, (block_start + frame_offset) >> 20, 
            (block_start + frame_offset));

            /* Video frame start = current location + frame offset */
            video->frame_offsets[frame_num] = ftell(video->file) + frame_offset;

            frame_total++;
        }

        /* Move to next block */
        fseek(video->file, next_block, SEEK_SET);

        if (limit != 0 && frame_total == limit) break;
    }

    video->is_active = 1;
}


void printMlvInfo(mlvObject_t * video)
{
    printf("\nMLV Info\n\n");
    printf("      MLV Version: %s\n", video->MLVI.versionString);
    printf("      File Blocks: %i\n", video->block_num);
    printf("\nLens Info\n\n");
    printf("       Lens Model: %s\n", video->LENS.lensName);
    printf("    Serial Number: %s\n", video->LENS.lensSerial);
    printf("\nCamera Info\n\n");
    printf("     Camera Model: %s\n", video->IDNT.cameraName);
    printf("    Serial Number: %s\n", video->IDNT.cameraSerial);
    printf("\nVideo Info\n\n");
    printf("     X Resolution: %i\n", video->RAWI.xRes);
    printf("     Y Resolution: %i\n", video->RAWI.yRes);
    printf("     Total Frames: %i\n", video->frames);
    printf("       Frame Rate: %.3f\n", video->frame_rate);
    printf("\nExposure Info\n\n");
    printf("          Shutter: 1/%.1f\n", (float)1000000 / (float)video->EXPO.shutterValue);
    printf("      ISO Setting: %i\n", video->EXPO.isoValue);
    printf("     Digital Gain: %i\n", video->EXPO.digitalGain);
    printf("\nRAW Info\n\n");
    printf("      Black Level: %i\n", video->RAWI.raw_info.black_level);
    printf("      White Level: %i\n", video->RAWI.raw_info.white_level);
    printf("     Bits / Pixel: %i\n\n", video->RAWI.raw_info.bits_per_pixel);
}