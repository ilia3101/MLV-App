#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>

#if defined(__linux)
#include <alloca.h>
#endif

#include "video_mlv.h"
#include "raw.h"
#include "mlv.h"
#include "llrawproc/llrawproc.h"

/* Debayering module */
#include "../debayer/debayer.h"
/* Processing module */
#include "../processing/raw_processing.h"

/* Lossless decompression */
#include "liblj92/lj92.h"

#define ROR32(v,a) ((v) >> (a) | (v) << (32-(a)))

static uint64_t file_set_pos(FILE *stream, uint64_t offset, int whence)
{
#if defined(__WIN32)
    return fseeko64(stream, offset, whence);
#else
    return fseek(stream, offset, whence);
#endif
}

static uint64_t file_get_pos(FILE *stream)
{
#if defined(__WIN32)
    return ftello64(stream);
#else
    return ftell(stream);
#endif
}

#ifndef STDOUT_SILENT
#define DEBUG(CODE) CODE
#else
#define DEBUG(CODE)
#endif

/* Just to be separate */
#include "audio_mlv.c"

/* Spanned multichunk MLV file handling */
static FILE **load_all_chunks(char *base_filename, int *entries)
{
    int seq_number = 0;
    int max_name_len = strlen(base_filename) + 16;
    char *filename = alloca(max_name_len);

    strncpy(filename, base_filename, max_name_len - 1);
    FILE **files = malloc(sizeof(FILE*));

    files[0] = fopen(filename, "rb");
    if(!files[0])
    {
        free(filename);
        free(files);
        return NULL;
    }

    DEBUG( printf("\nFile %s opened\n", filename); )

    /* get extension and check if it is a .MLV */
    char *dot = strrchr(filename, '.');
    if(dot)
    {
        dot++;
        if(strcasecmp(dot, "mlv"))
        {
            seq_number = 100;
        }
    }

    (*entries)++;
    while(seq_number < 99)
    {
        FILE **realloc_files = realloc(files, (*entries + 1) * sizeof(FILE*));

        if(!realloc_files)
        {
            free(filename);
            free(files);
            return NULL;
        }

        files = realloc_files;

        /* check for the next file M00, M01 etc */
        char seq_name[8];

        sprintf(seq_name, "%02d", seq_number);
        seq_number++;

        strcpy(&filename[strlen(filename) - 2], seq_name);

        /* try to open */
        files[*entries] = fopen(filename, "rb");
        if(files[*entries])
        {
            DEBUG( printf("File %s opened\n", filename); )
            (*entries)++;
        }
        else
        {
            DEBUG( printf("File %s not existing\n\n", filename); )
            break;
        }
    }

    return files;
}

static void close_all_chunks(FILE ** files, int entries)
{
    for(int i = 0; i < entries; i++)
        if(files[i]) fclose(files[i]);
    if(files) free(files);
}

static void frame_index_sort(frame_index_t *frame_index, uint32_t entries)
{
    if (!entries) return;

    uint32_t n = entries;
    do
    {
        uint32_t new_n = 1;
        for (uint32_t i = 0; i < n-1; ++i)
        {
            if (frame_index[i].frame_time > frame_index[i+1].frame_time)
            {
                frame_index_t tmp = frame_index[i+1];
                frame_index[i+1] = frame_index[i];
                frame_index[i] = tmp;
                new_n = i + 1;
            }
        }
        n = new_n;
    } while (n > 1);
}

/* Unpacks the bits of a frame to get a bayer B&W image (without black level correction)
 * Needs memory to return to, sized: sizeof(float) * getMlvHeight(urvid) * getMlvWidth(urvid)
 * Output image's pixels will be in range 0-65535 as if it is 16 bit integers */
void getMlvRawFrameFloat(mlvObject_t * video, uint64_t frameIndex, float * outputFrame)
{
    int bitdepth = video->RAWI.raw_info.bits_per_pixel;
    int width = video->RAWI.xRes;
    int height = video->RAWI.yRes;
    int pixels_count = width * height;

    int chunk = video->video_index[frameIndex].chunk_num;
    uint32_t frame_size = video->video_index[frameIndex].frame_size;
    uint64_t frame_offset = video->video_index[frameIndex].frame_offset;

    /* How many bytes is RAW frame */
    int raw_frame_size = (width * height * bitdepth) / 8;
    int unpacked_frame_size = width * height * sizeof(uint16_t);

    /* Memory buffer for original RAW data */
    uint8_t * raw_frame = (uint8_t *)malloc(raw_frame_size + 4); // additional 4 bytes for safety
    /* Memory buffer for decompressed or bit unpacked RAW data */
    uint16_t * unpacked_frame = NULL;

    FILE * file = video->file[chunk];

    /* Move to start of frame in file and read the RAW data */
    pthread_mutex_lock(video->main_file_mutex + chunk);
    file_set_pos(file, frame_offset, SEEK_SET);

    if (video->MLVI.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92)
    {
        fread(raw_frame, sizeof(uint8_t), frame_size, file);
        pthread_mutex_unlock(video->main_file_mutex + chunk);

        int components = 1;
        lj92 decoder_object;
        int ret = lj92_open(&decoder_object, raw_frame, frame_size, &width, &height, &bitdepth, &components);
        if(ret != LJ92_ERROR_NONE)
        {
            DEBUG( printf("LJ92 decoder: Failed with error code (%d)\n", ret); )
            memset(outputFrame, 0, pixels_count * sizeof(float));
            goto err_out;
        }
        else
        {
            unpacked_frame = (uint16_t *)malloc( unpacked_frame_size );
            ret = lj92_decode(decoder_object, unpacked_frame, width * height * components, 0, NULL, 0);
            if(ret != LJ92_ERROR_NONE)
            {
                DEBUG( printf("LJ92 decoder: Failed with error code (%d)\n", ret); )
                memset(outputFrame, 0, pixels_count * sizeof(float));
                goto err_out;
            }
        }
        lj92_close(decoder_object);
    }
    else /* If not compressed just unpack to 16bit */
    {
        fread(raw_frame, sizeof(uint8_t), raw_frame_size, file);
        pthread_mutex_unlock(video->main_file_mutex + chunk);

        unpacked_frame = (uint16_t *)malloc( unpacked_frame_size );
        uint32_t mask = (1 << bitdepth) - 1;
        for (int i = 0; i < pixels_count; ++i)
        {
            uint32_t bits_offset = i * bitdepth;
            uint32_t bits_address = bits_offset / 16;
            uint32_t bits_shift = bits_offset % 16;
            uint32_t rotate_value = 16 + ((32 - bitdepth) - bits_shift);
            uint32_t uncorrected_data = *((uint32_t *)&((uint16_t *)raw_frame)[bits_address]);
            uint32_t data = ROR32(uncorrected_data, rotate_value);
            unpacked_frame[i] = (uint16_t)(data & mask);
        }
    }

    /* apply low level raw processing to the unpacked_frame */
    applyLLRawProcObject(video, unpacked_frame, unpacked_frame_size);

    /* high quality dualiso buffer consists of real 16 bit values, no converting needed */
    int shift_val = (llrpHQDualIso(video)) ? 0 : (16 - bitdepth);

    /* convert uint16_t raw data -> float raw_data for processing with amaze or bilinear debayer, both need data input as float */
    for (int i = 0; i < pixels_count; ++i)
    {
        outputFrame[i] = (float)(unpacked_frame[i] << shift_val);
    }

err_out:
    if(unpacked_frame) free(unpacked_frame);
    free(raw_frame);
}

void setMlvProcessing(mlvObject_t * video, processingObject_t * processing)
{
    //double camera_matrix[9]; commented for now, not used

    /* Easy bit */
    video->processing = processing;

    /* MATRIX stuff (not working, so commented out - 
     * processing object defaults to 1,0,0,0,1,0,0,0,1) */

    /* Get camera matrix for MLV clip and set it in the processing object */
    //getMlvCameraTosRGBMatrix(video, camera_matrix);
    /* Set Camera to RGB */
    //processingCamTosRGBMatrix(processing, camera_matrix); /* Still not used in processing cos not working right */

    /* BLACK / WHITE level */
    processingSetBlackAndWhiteLevel( processing, 
                                     getMlvBlackLevel(video) * 4,
                                     getMlvWhiteLevel(video) * 4 );

    /* If 5D3 or cropmode */
    if (strlen((char *)getMlvCamera(video)) > 20 || getMlvMaxWidth(video) > 1920)
    {
        processingSetSharpeningBias(processing, 0.0);
    }
    else /* Sharpening more sideways to hide vertical line skip artifacts a bit */
    {
        processingSetSharpeningBias(processing, -0.33);
    }
}

void getMlvRawFrameDebayered(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame)
{
    int width = getMlvWidth(video);
    int height = getMlvHeight(video);
    int frame_size = width * height * sizeof(uint16_t) * 3;

    /* If frame was requested last time and is sitting in the "current" frame cache */
    if ( video->cached_frames[frameIndex] == MLV_FRAME_NOT_CACHED
         && video->current_cached_frame_active 
         && video->current_cached_frame == frameIndex )
    {
        memcpy(outputFrame, video->rgb_raw_current_frame, frame_size);
    }
    /* Is this next bit even readable? */
    else switch (video->cached_frames[frameIndex])
    {
        case MLV_FRAME_IS_CACHED:
        {
            memcpy(outputFrame, video->rgb_raw_frames[frameIndex], frame_size);
            break;
        }

        /* Else cache it or store in the 'current frame' */
        case MLV_FRAME_NOT_CACHED:
        {
            /* If it is within the cache range, request for it to be cached */
            if (isMlvObjectCaching(video) && frameIndex < getMlvRawCacheLimitFrames(video))
            {
                video->cache_next = frameIndex;
            }
        }

        case MLV_FRAME_BEING_CACHED:
        {
            if (doesMlvAlwaysUseAmaze(video) && isMlvObjectCaching(video))
            {
                while (video->cached_frames[frameIndex] != MLV_FRAME_IS_CACHED) usleep(100);
                memcpy(outputFrame, video->rgb_raw_frames[frameIndex], frame_size);
            }
            else
            {
                float * raw_frame = malloc(width * height * sizeof(float));
                get_mlv_raw_frame_debayered(video, frameIndex, raw_frame, video->rgb_raw_current_frame, doesMlvAlwaysUseAmaze(video));
                free(raw_frame);
                memcpy(outputFrame, video->rgb_raw_current_frame, frame_size);
                video->current_cached_frame_active = 1;
                video->current_cached_frame = frameIndex;
            }
            break;
        }
    }
}

/* Get a processed frame in 16 bit */
void getMlvProcessedFrame16(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame)
{
    /* Useful */
    int width = getMlvWidth(video);
    int height = getMlvHeight(video);

    /* Size of RAW frame */
    int rgb_frame_size = height * width * 3;

    /* Unprocessed debayered frame (RGB) */
    uint16_t * unprocessed_frame = malloc( rgb_frame_size * sizeof(uint16_t) );

    /* Get the raw data in B&W */
    getMlvRawFrameDebayered(video, frameIndex, unprocessed_frame);

    /* Do processing.......... */
    applyProcessingObject( video->processing,
                           width, height,
                           unprocessed_frame,
                           outputFrame );

    free(unprocessed_frame);
}

/* Get a processed frame in 8 bit */
void getMlvProcessedFrame8(mlvObject_t * video, uint64_t frameIndex, uint8_t * outputFrame)
{
    /* Size of RAW frame */
    int rgb_frame_size = getMlvWidth(video) * getMlvHeight(video) * 3;

    /* Processed frame (RGB) */
    uint16_t * processed_frame = malloc( rgb_frame_size * sizeof(uint16_t) );

    getMlvProcessedFrame16(video, frameIndex, processed_frame);

    /* Copy (and 8-bitize) */
    for (int i = 0; i < rgb_frame_size; ++i)
    {
        outputFrame[i] = processed_frame[i] >> 8;
    }

    free(processed_frame);
}

/* To initialise mlv object with a clip
 * Two functions in one */
mlvObject_t * initMlvObjectWithClip(char * mlvPath, int * err, int preview)
{
    mlvObject_t * video = initMlvObject();
    *err = openMlvClip(video, mlvPath, preview);
    return video;
}

/* Allocates a tiny bit of memory for everything in the structure
 * so we can always be sure there is memory, and when we need to 
 * resize it, simply do free followed by malloc */
mlvObject_t * initMlvObject()
{
    mlvObject_t * video = (mlvObject_t *)calloc( 1, sizeof(mlvObject_t) );

    /* Initialize index buffers with NULL,
     * will be allocated/reallocated later */
    video->video_index = NULL;
    video->audio_index = NULL;

    /* Cache things, only one element for now as it is empty */
    video->rgb_raw_frames = NULL;
    video->rgb_raw_current_frame = NULL;
    video->cached_frames = NULL;
    /* All frames in one block of memory for least mallocing during usage */
    video->cache_memory_block = NULL;
    /* Path (so separate cache threads can have their own FILE*s) */
    video->path = NULL;

    /* Will avoid main file conflicts with audio and stuff */
    pthread_mutex_init(&video->g_mutexFind, NULL);
    pthread_mutex_init(&video->g_mutexCount, NULL);
    pthread_mutex_init(&video->cache_mutex, NULL);

    /* Set cache limit to allow ~1 second of 1080p and be safe for low ram PCs */
    setMlvRawCacheLimitMegaBytes(video, 290);
    setMlvCacheStartFrame(video, 0); /* Just in case */

    /* Seems about right */
    setMlvCpuCores(video, 4);

    /* init low level raw processing object */
    video->llrawproc = initLLRawProcObject();

    /* Retun pointer */
    return video;
}

/* Free all memory and close file */
void freeMlvObject(mlvObject_t * video)
{
    isMlvActive(video) = 0;

    /* Stop caching and make sure using silly sleep trick */
    video->stop_caching = 1;
    while (video->cache_thread_count) usleep(100);

    /* Close all MLV file chunks */
    if(video->file) close_all_chunks(video->file, video->filenum);
    /* Free all memory */
    if(video->video_index) free(video->video_index);
    if(video->audio_index) free(video->audio_index);

    /* Now free these */
    if(video->cached_frames){

        free(video->cached_frames);
        video->cached_frames = NULL;
    }
    if(video->rgb_raw_frames) free(video->rgb_raw_frames);
    if(video->rgb_raw_current_frame) free(video->rgb_raw_current_frame);
    if(video->cache_memory_block) free(video->cache_memory_block);
    if(video->path) free(video->path);
    freeLLRawProcObject(video->llrawproc);

    /* Mutex things here... */
    for (int i = 0; i < video->filenum; ++i)
        pthread_mutex_destroy(video->main_file_mutex + i);
    if(video->main_file_mutex) free(video->main_file_mutex);
    pthread_mutex_destroy(&video->g_mutexFind);
    pthread_mutex_destroy(&video->g_mutexCount);
    pthread_mutex_destroy(&video->cache_mutex);

    /* Main 1 */
    free(video);
}

/* Save MLV App map file (.MAPP) */
static int save_mapp(mlvObject_t * video)
{
    int mapp_name_len = strlen(video->path);
    char * mapp_filename = alloca(mapp_name_len + 2);
    memcpy(mapp_filename, video->path, mapp_name_len);
    char * dot = strrchr(mapp_filename, '.');
    memcpy(dot, ".MAPP\0", 6);

    size_t video_index_size = video->frames * sizeof(frame_index_t);
    size_t audio_index_size = video->audios * sizeof(frame_index_t);
    size_t mapp_buf_size = sizeof(mapp_header_t) + video_index_size + audio_index_size + 596;

    uint8_t * mapp_buf = malloc(mapp_buf_size);
    if(!mapp_buf)
    {
        return 1;
    }

    /* init mapp header */
    mapp_header_t mapp_header = { "MAPP", sizeof(mapp_header_t), video->frames, video->audios };

    /* fill mapp buffer */
    uint8_t * ptr = mapp_buf;
    memcpy(ptr, (uint8_t*)&mapp_header, sizeof(mapp_header_t));
    ptr += sizeof(mapp_header_t);
    if(video->video_index)
    {
        memcpy(ptr, (uint8_t*)video->video_index, video_index_size);
        ptr += video_index_size;
    }
    if(video->audio_index)
    {
        memcpy(ptr, (uint8_t*)video->audio_index, audio_index_size);
        ptr += audio_index_size;
    }
    memcpy(ptr, &(video->MLVI), sizeof(mlv_file_hdr_t));
    memcpy(ptr += sizeof(mlv_file_hdr_t), (uint8_t*)&(video->RAWI), sizeof(mlv_rawi_hdr_t));
    memcpy(ptr += sizeof(mlv_rawi_hdr_t), (uint8_t*)&(video->RAWC), sizeof(mlv_rawc_hdr_t));
    memcpy(ptr += sizeof(mlv_rawc_hdr_t), (uint8_t*)&(video->IDNT), sizeof(mlv_idnt_hdr_t));
    memcpy(ptr += sizeof(mlv_idnt_hdr_t), (uint8_t*)&(video->EXPO), sizeof(mlv_expo_hdr_t));
    memcpy(ptr += sizeof(mlv_expo_hdr_t), (uint8_t*)&(video->LENS), sizeof(mlv_lens_hdr_t));
    memcpy(ptr += sizeof(mlv_lens_hdr_t), (uint8_t*)&(video->RTCI), sizeof(mlv_rtci_hdr_t));
    memcpy(ptr += sizeof(mlv_rtci_hdr_t), (uint8_t*)&(video->WBAL), sizeof(mlv_wbal_hdr_t));
    memcpy(ptr += sizeof(mlv_wbal_hdr_t), (uint8_t*)&(video->DISO), sizeof(mlv_diso_hdr_t));

    /* open .MAPP file for writing */
    FILE* mappf = fopen(mapp_filename, "wb");
    if (!mappf)
    {
        free(mapp_buf);
        return 1;
    }

    /* write mapp buffer */
    if(fwrite(mapp_buf, mapp_buf_size, 1, mappf) != 1)
    {
        fclose(mappf);
        free(mapp_buf);
        return 1;
    }

    DEBUG( printf("\nMAPP saved: %s\n", mapp_filename); )

    fclose(mappf);
    free(mapp_buf);
    return 0;
}

/* Load MLV App map file (.MAPP) */
static int load_mapp(mlvObject_t * video)
{
    int mapp_name_len = strlen(video->path);
    char * mapp_filename = alloca(mapp_name_len + 2);
    memcpy(mapp_filename, video->path, mapp_name_len);
    char * dot = strrchr(mapp_filename, '.');
    memcpy(dot, ".MAPP\0", 6);

    /* open .MAPP file for reading */
    FILE* mappf = fopen(mapp_filename, "rb");
    if (!mappf)
    {
        return 1;
    }

    /* Read .MAPP header */
    mapp_header_t mapp_header;
    if ( fread(&mapp_header, sizeof(mapp_header_t), 1, mappf) != 1 )
    {
        DEBUG( printf("Could not read: %s\n", video->path); )
        goto mapp_error;
    }

    /* Read video index */
    if(mapp_header.video_frames)
    {
        size_t video_index_size = mapp_header.video_frames * sizeof(frame_index_t);

        video->video_index = malloc(video_index_size);
        if(!video->video_index)
        {
            DEBUG( printf("Malloc error: video index\n"); )
            goto mapp_error;        }

        if ( fread(video->video_index, video_index_size, 1, mappf) != 1 )
        {
            DEBUG( printf("Could not read: %s\n", video->path); )
            goto mapp_error;        }
    }

    /* Read audio index */
    if(mapp_header.audio_frames)
    {
        size_t audio_index_size = mapp_header.audio_frames * sizeof(frame_index_t);

        video->audio_index = malloc(audio_index_size);
        if(!video->audio_index)
        {
            DEBUG( printf("Malloc error: audio index\n"); )
            goto mapp_error;
        }

        if ( fread(video->audio_index, audio_index_size, 1, mappf) != 1 )
        {
            DEBUG( printf("Could not read: %s\n", video->path); )
            goto mapp_error;
        }
    }

    /* Read MLV block headers */
    int ret = 0;
    ret += fread(&(video->MLVI), sizeof(mlv_file_hdr_t), 1, mappf);
    ret += fread(&(video->RAWI), sizeof(mlv_rawi_hdr_t), 1, mappf);
    ret += fread(&(video->RAWC), sizeof(mlv_rawc_hdr_t), 1, mappf);
    ret += fread(&(video->IDNT), sizeof(mlv_idnt_hdr_t), 1, mappf);
    ret += fread(&(video->EXPO), sizeof(mlv_expo_hdr_t), 1, mappf);
    ret += fread(&(video->LENS), sizeof(mlv_lens_hdr_t), 1, mappf);
    ret += fread(&(video->RTCI), sizeof(mlv_rtci_hdr_t), 1, mappf);
    ret += fread(&(video->WBAL), sizeof(mlv_wbal_hdr_t), 1, mappf);
    ret += fread(&(video->DISO), sizeof(mlv_diso_hdr_t), 1, mappf);
    if(ret != 9)
    {
        DEBUG( printf("Could not read: %s\n", video->path); )
        goto mapp_error;
    }

    /* Set video and audio frame counts */
    video->frames = mapp_header.video_frames;
    video->audios = mapp_header.audio_frames;

    DEBUG( printf("MAPP loaded: %s\n", mapp_filename); )

    fclose(mappf);
    return 0;

mapp_error:

    if(video->video_index)
    {
        free(video->video_index);
        video->video_index = NULL;
    }
    if(video->audio_index)
    {
        free(video->audio_index);
        video->audio_index = NULL;
    }
    if(mappf) fclose(mappf);

    return 1;
}

/* Reads an MLV file in to a mlv object(mlvObject_t struct) 
 * only puts metadata in to the mlvObject_t, 
 * no debayering or bit unpacking */
int openMlvClip(mlvObject_t * video, char * mlvPath, int open_mode)
{
    video->path = malloc( strlen(mlvPath) + 1 );
    memcpy(video->path, mlvPath, strlen(mlvPath));
    video->path[strlen(mlvPath)] = 0x0;
    video->file = load_all_chunks(mlvPath, &video->filenum);
    if(!video->file)
    {
        DEBUG( printf("\nCould not open file: %s\n", video->path); )
        return MLV_ERR_OPEN; // can not open file
    }

    if(!load_mapp(video)) goto short_cut;

    mlv_hdr_t block_header; /* Basic MLV block header */
    int block_num = 0; /* Number of blocks in file */
    uint64_t video_frames = 0; /* Number of frames in video */
    uint64_t audio_frames = 0; /* Number of audio blocks in video */
    uint64_t video_index_max = 0; /* initial size of frame index */
    uint64_t audio_index_max = 0; /* initial size of audio index */
    int rtci_read = 0; /* Flips to 1 if 1st RTCI block was read */

    for(int i = 0; i < video->filenum; i++)
    {
        /* Getting size of file in bytes */
        file_set_pos(video->file[i], 0, SEEK_END);
        uint64_t file_size = file_get_pos(video->file[i]);
        if ( !file_size )
        {
            DEBUG( printf("\nZero byte size file: %s\n", video->path); )
            --video->filenum;
            return MLV_ERR_INVALID;
        }
        file_set_pos(video->file[i], 0, SEEK_SET); /* Start of file */

        /* Read file header */
        if ( fread(&block_header, sizeof(mlv_hdr_t), 1, video->file[i]) != 1 )
        {
            DEBUG( printf("\nFile is too short to be a valid MLV: %s\n", video->path); )
            --video->filenum;
            return MLV_ERR_INVALID;
        }
        file_set_pos(video->file[i], 0, SEEK_SET); /* Start of file */

        if ( memcmp(block_header.blockType, "MLVI", 4) == 0 )
        {
            fread(&video->MLVI, sizeof(mlv_file_hdr_t), 1, video->file[i]);
        }
        else
        {
            DEBUG( printf("\nFile header is missing, invalid MLV: %s\n", video->path); )
            --video->filenum;
            return MLV_ERR_INVALID;
        }

        while ( file_get_pos(video->file[i]) < file_size ) /* Check if were at end of file yet */
        {
            /* Record position to go back to it later if block is read */
            uint64_t block_start = file_get_pos(video->file[i]);
            /* Read block header */
            fread(&block_header, sizeof(mlv_hdr_t), 1, video->file[i]);
            /* Next block location */
            uint64_t next_block = (uint64_t)block_start + (uint64_t)block_header.blockSize;
            /* Go back to start of block for next bit */
            file_set_pos(video->file[i], block_start, SEEK_SET);

            /* Now check what kind of block it is and read it in to the mlv object */
            if ( memcmp(block_header.blockType, "VIDF", 4) == 0 )
            {
                fread(&video->VIDF, sizeof(mlv_vidf_hdr_t), 1, video->file[i]);

                DEBUG( printf("video frame %i | chunk %i | size %lu | offset %lu | time %lu\n",
                               video->VIDF.frameNumber, i, video->VIDF.blockSize - sizeof(mlv_vidf_hdr_t) - video->VIDF.frameSpace,
                               block_start + video->VIDF.frameSpace, video->VIDF.timestamp); )

                /* Dynamically resize the frame index buffer */
                if(!video_index_max)
                {
                    video_index_max = 128;
                    video->video_index = (frame_index_t *)calloc(video_index_max, sizeof(frame_index_t));
                }
                else if(video_frames >= video_index_max - 1)
                {
                    uint64_t video_index_new_size = video_index_max * 2;
                    frame_index_t * video_index_new = (frame_index_t *)calloc(video_index_new_size, sizeof(frame_index_t));
                    memcpy(video_index_new, video->video_index, video_index_max * sizeof(frame_index_t));
                    free(video->video_index);
                    video->video_index = video_index_new;
                    video_index_max = video_index_new_size;
                }

                /* Fill frame index */
                video->video_index[video_frames].frame_type = 1;
                video->video_index[video_frames].chunk_num = i;
                video->video_index[video_frames].frame_size = video->VIDF.blockSize - sizeof(mlv_vidf_hdr_t) - video->VIDF.frameSpace;
                video->video_index[video_frames].frame_offset = file_get_pos(video->file[i]) + video->VIDF.frameSpace;
                video->video_index[video_frames].frame_time = video->VIDF.timestamp;

                /* Count actual video frames */
                video_frames++;

                /* In preview mode exit loop after first videf read */
                if(open_mode == MLV_OPEN_PREVIEW)
                {
                    video->frames = video_frames;
                    video->audios = audio_frames;
                    goto short_cut;
                }
            }
            else if ( memcmp(block_header.blockType, "AUDF", 4) == 0 )
            {
                fread(&video->AUDF, sizeof(mlv_audf_hdr_t), 1, video->file[i]);

                DEBUG( printf("audio frame %i | chunk %i | size %lu | offset %lu | time %lu\n",
                               video->AUDF.frameNumber, i, video->AUDF.blockSize - sizeof(mlv_audf_hdr_t) - video->AUDF.frameSpace,
                               block_start + video->AUDF.frameSpace, video->AUDF.timestamp); )

                /* Dynamically resize the audio index buffer */
                if(!audio_index_max)
                {
                    audio_index_max = 32;
                    video->audio_index = (frame_index_t *)malloc(sizeof(frame_index_t) * audio_index_max);
                }
                else if(audio_frames >= audio_index_max - 1)
                {
                    uint64_t audio_index_new_size = audio_index_max * 2;
                    frame_index_t * audio_index_new = (frame_index_t *)calloc(audio_index_new_size, sizeof(frame_index_t));
                    memcpy(audio_index_new, video->audio_index, audio_index_max * sizeof(frame_index_t));
                    free(video->audio_index);
                    video->audio_index = audio_index_new;
                    audio_index_max = audio_index_new_size;
                }

                /* Fill audio index */
                video->audio_index[audio_frames].frame_type = 2;
                video->audio_index[audio_frames].chunk_num = i;
                video->audio_index[audio_frames].frame_size = video->AUDF.blockSize - sizeof(mlv_audf_hdr_t) - video->AUDF.frameSpace;
                video->audio_index[audio_frames].frame_offset = file_get_pos(video->file[i]) + video->AUDF.frameSpace;
                video->audio_index[audio_frames].frame_time = video->AUDF.timestamp;

                /* Count actual audio frames */
                audio_frames++;
            }
            else if ( memcmp(block_header.blockType, "RAWI", 4) == 0 )
            {
                fread(&video->RAWI, sizeof(mlv_rawi_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "RAWC", 4) == 0 )
            {
                fread(&video->RAWC, sizeof(mlv_rawc_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "WAVI", 4) == 0 )
            {
                fread(&video->WAVI, sizeof(mlv_wavi_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "EXPO", 4) == 0 )
            {
                fread(&video->EXPO, sizeof(mlv_expo_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "LENS", 4) == 0 )
            {
                fread(&video->LENS, sizeof(mlv_lens_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "WBAL", 4) == 0 )
            {
                fread(&video->WBAL, sizeof(mlv_wbal_hdr_t), 1, video->file[i]);
            }
            else if ( ( memcmp(block_header.blockType, "RTCI", 4) == 0 ) && ( !rtci_read ) )
            {
                fread(&video->RTCI, sizeof(mlv_rtci_hdr_t), 1, video->file[i]);
                rtci_read = 1; //read only first one
            }
            else if ( memcmp(block_header.blockType, "IDNT", 4) == 0 )
            {
                fread(&video->IDNT, sizeof(mlv_idnt_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "INFO", 4) == 0 )
            {
                fread(&video->INFO, sizeof(mlv_info_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "DISO", 4) == 0 )
            {
                fread(&video->DISO, sizeof(mlv_diso_hdr_t), 1, video->file[i]);
            }

            /* Printing stuff for fun */
            //DEBUG( printf("Block #%4i  |  %.4s  |%9i Bytes\n", block_num, block_header.blockType, block_header.blockSize); )

            /* Move to next block */
            file_set_pos(video->file[i], next_block, SEEK_SET);

            block_num++;
        }
    }

    /* Sort video and audio frames by time stamp */
    if(video_frames) frame_index_sort(video->video_index, video_frames);
    if(audio_frames) frame_index_sort(video->audio_index, audio_frames);

    /* Set frame count in video object */
    video->frames = video_frames;
    /* Set audio count in video object */
    video->audios = audio_frames;

    /* Save mapp file if this feature is on */
    if(open_mode == MLV_OPEN_MAPP) save_mapp(video);

short_cut:

    /* back up black and white levels */
    video->llrawproc->mlv_black_level = getMlvBlackLevel(video);
    video->llrawproc->mlv_white_level = getMlvWhiteLevel(video);

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
    }

    /* Lowering white level a bit avoids pink grain in highlihgt reconstruction */
    getMlvWhiteLevel(video) = (double)getMlvWhiteLevel(video) * 0.993;

    /* Mutexes for every file */
    video->main_file_mutex = calloc(sizeof(pthread_mutex_t), video->filenum);
    for (int i = 0; i < video->filenum; ++i)
        pthread_mutex_init(video->main_file_mutex + i, NULL);

    /* Set total block amount in mlv */
    video->block_num = block_num;
    /* NON compressed frame size */
    video->frame_size = (getMlvHeight(video) * getMlvWidth(video) * getMlvBitdepth(video)) / 8;
    /* Calculate framerate */
    video->frame_rate = (double)video->MLVI.sourceFpsNom / (double)video->MLVI.sourceFpsDenom;

    /* Make sure frame cache number is up to date by rerunning thiz */
    setMlvRawCacheLimitMegaBytes(video, getMlvRawCacheLimitMegaBytes(video));

    /* For frame cache */
    video->rgb_raw_frames = (uint16_t **)malloc( sizeof(uint16_t *) * video->frames );
    video->rgb_raw_current_frame = (uint16_t *)malloc( getMlvWidth(video) * getMlvHeight(video) * 3 * sizeof(uint16_t) );
    video->cached_frames = (uint8_t *)calloc( sizeof(uint8_t), video->frames );

    isMlvActive(video) = 1;

    /* Start caching unless it was disabled already */
    if (!video->stop_caching && (open_mode != MLV_OPEN_PREVIEW))
    {
        for (int i = 0; i < video->cpu_cores; ++i)
        {
            add_mlv_cache_thread(video);
        }
    }

    return MLV_ERR_NONE;
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
