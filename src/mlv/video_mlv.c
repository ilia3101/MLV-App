#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <math.h>

#if defined(__linux)
#include <alloca.h>
extern int usleep (__useconds_t __useconds);
#else
#include <unistd.h>
#endif

#include "video_mlv.h"
#include "audio_mlv.h"

#include "raw.h"
#include "mlv.h"
#include "llrawproc/llrawproc.h"

/* Debayering module */
#include "../debayer/debayer.h"
/* Processing module */
#include "../processing/raw_processing.h"

/* Lossless decompression */
#include "liblj92/lj92.h"

/* Bitunpack and lossless compression */
#include "../dng/dng.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
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

#ifdef __WIN32
#define FMT_SIZE "%u"
#else
#define FMT_SIZE "%zu"
#endif

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

/* Unpack or decompress original raw data */
int getMlvRawFrameUint16(mlvObject_t * video, uint64_t frameIndex, uint16_t * unpackedFrame)
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

    /* Memory buffer for original RAW data */
    uint8_t * raw_frame = (uint8_t *)malloc(raw_frame_size + 4); // additional 4 bytes for safety

    FILE * file = video->file[chunk];

    /* Move to start of frame in file and read the RAW data */
    pthread_mutex_lock(video->main_file_mutex + chunk);
    file_set_pos(file, frame_offset, SEEK_SET);

    if (video->MLVI.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92)
    {
        if(fread(raw_frame, frame_size, 1, file) != 1)
        {
            DEBUG( printf("Frame data read error\n"); )
            free(raw_frame);
            return 1;
        }

        pthread_mutex_unlock(video->main_file_mutex + chunk);

        int components = 1;
        lj92 decoder_object;
        int ret = lj92_open(&decoder_object, raw_frame, frame_size, &width, &height, &bitdepth, &components);
        if(ret != LJ92_ERROR_NONE)
        {
            DEBUG( printf("LJ92 decoder: Failed with error code (%d)\n", ret); )
            free(raw_frame);
            return 1;
        }
        else
        {
            ret = lj92_decode(decoder_object, unpackedFrame, width * height * components, 0, NULL, 0);
            if(ret != LJ92_ERROR_NONE)
            {
                DEBUG( printf("LJ92 decoder: Failed with error code (%d)\n", ret); )
                free(raw_frame);
                return 1;

            }
        }
        lj92_close(decoder_object);
    }
    else /* If not compressed just unpack to 16bit */
    {
        if(fread(raw_frame, raw_frame_size, 1, file) != 1)
        {
            DEBUG( printf("Frame data read error\n"); )
            free(raw_frame);
            return 1;
        }

        pthread_mutex_unlock(video->main_file_mutex + chunk);

        uint32_t mask = (1 << bitdepth) - 1;
        for (int i = 0; i < pixels_count; ++i)
        {
            uint32_t bits_offset = i * bitdepth;
            uint32_t bits_address = bits_offset / 16;
            uint32_t bits_shift = bits_offset % 16;
            uint32_t rotate_value = 16 + ((32 - bitdepth) - bits_shift);
            uint32_t uncorrected_data = *((uint32_t *)&((uint16_t *)raw_frame)[bits_address]);
            uint32_t data = ROR32(uncorrected_data, rotate_value);
            unpackedFrame[i] = (uint16_t)(data & mask);
        }
    }

    free(raw_frame);
    return 0;
}

/* Unpacks the bits of a frame to get a bayer B&W image (without black level correction)
 * Needs memory to return to, sized: sizeof(float) * getMlvHeight(urvid) * getMlvWidth(urvid)
 * Output image's pixels will be in range 0-65535 as if it is 16 bit integers */
void getMlvRawFrameFloat(mlvObject_t * video, uint64_t frameIndex, float * outputFrame)
{
    int pixels_count = video->RAWI.xRes * video->RAWI.yRes;

    /* Memory buffer for decompressed or bit unpacked RAW data */
    size_t unpacked_frame_size = pixels_count * 2;
    uint16_t * unpacked_frame = (uint16_t *)malloc( unpacked_frame_size );

    if(getMlvRawFrameUint16(video, frameIndex, unpacked_frame))
    {
        memset(outputFrame, 0, pixels_count * sizeof(float));
        free(unpacked_frame);
        return;
    }

    /* apply low level raw processing to the unpacked_frame */
    applyLLRawProcObject(video, unpacked_frame, unpacked_frame_size);

    /* high quality dualiso buffer consists of real 16 bit values, no converting needed */
    int shift_val = (llrpHQDualIso(video)) ? 0 : (16 - video->RAWI.raw_info.bits_per_pixel);

    /* convert uint16_t raw data -> float raw_data for processing with amaze or bilinear debayer, both need data input as float */
    for (volatile int i = 0; i < pixels_count; ++i)
    {
        outputFrame[i] = (float)(unpacked_frame[i] << shift_val);
    }

    free(unpacked_frame);
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

    /* Corrected RAW black/white levels */
    int CorrectedBlackLevel = getMlvBlackLevel(video);
    int CorrectedWhiteLevel = getMlvWhiteLevel(video);

    if(CorrectedBlackLevel && CorrectedWhiteLevel)
    {
        /* We work in an imaginary 14 bit world, so if its 10/12 bit, blackwhite levels shall be multiplied */
        switch(getMlvBitdepth(video))
        {
            case 10:
                CorrectedBlackLevel *= 16;
                CorrectedWhiteLevel *= 16;
                break;
            case 12:
                CorrectedBlackLevel *= 4;
                CorrectedWhiteLevel *= 4;
                break;
        }

        /* let's be kind and repair black level if it's broken (OMG IT WORKS!) */
        if ((CorrectedBlackLevel < 1700) || (CorrectedBlackLevel > 2200))
        {
            /* Camera specific stuff */
            switch(getMlvCameraModel(video))
            {
                case 0x80000218: // 5D2
                case 0x80000261: // 50D
                    CorrectedBlackLevel = 1792;
                    break;
                default: // all other cameras
                    CorrectedBlackLevel = 2048;
                    break;
            }
        }

        /* Lowering white level a bit avoids pink grain in highlihgt reconstruction */
        CorrectedWhiteLevel = (int)((double)CorrectedWhiteLevel * 0.993);
    }

    /* BLACK / WHITE level */
    processingSetBlackAndWhiteLevel( processing, 
                                     CorrectedBlackLevel * 4,
                                     CorrectedWhiteLevel * 4 );

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

/* Get a processed frame in 16 bit, only use more than one thread for preview as
 * it may have minor artifacts (though I haven't found them yet) */
void getMlvProcessedFrame16(mlvObject_t * video, uint64_t frameIndex, uint16_t * outputFrame, int threads)
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
                           outputFrame,
                           threads, 1 );

    free(unprocessed_frame);
}

/* Get a processed frame in 8 bit */
void getMlvProcessedFrame8(mlvObject_t * video, uint64_t frameIndex, uint8_t * outputFrame, int threads)
{
    /* Size of RAW frame */
    int rgb_frame_size = getMlvWidth(video) * getMlvHeight(video) * 3;

    /* Processed frame (RGB) */
    uint16_t * processed_frame = malloc( rgb_frame_size * sizeof(uint16_t) );

    getMlvProcessedFrame16(video, frameIndex, processed_frame, threads);

    /* Copy (and 8-bitize) */
    for (int i = 0; i < rgb_frame_size; ++i)
    {
        outputFrame[i] = processed_frame[i] >> 8;
    }

    free(processed_frame);
}

/* To initialise mlv object with a clip
 * Two functions in one */
mlvObject_t * initMlvObjectWithClip(char * mlvPath, int preview, int * err, char * error_message)
{
    mlvObject_t * video = initMlvObject();
    *err = openMlvClip(video, mlvPath, preview, error_message);
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

    /* Init low level raw processing object */
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
    freeLLRawProcObject(video);

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
    size_t mapp_buf_size = sizeof(mapp_header_t) +
                           video_index_size +
                           audio_index_size +
                           sizeof(mlv_file_hdr_t) +
                           sizeof(mlv_rawi_hdr_t) +
                           sizeof(mlv_rawc_hdr_t) +
                           sizeof(mlv_idnt_hdr_t) +
                           sizeof(mlv_expo_hdr_t) +
                           sizeof(mlv_lens_hdr_t) +
                           sizeof(mlv_rtci_hdr_t) +
                           sizeof(mlv_wbal_hdr_t) +
                           sizeof(mlv_styl_hdr_t) +
                           sizeof(mlv_wavi_hdr_t) +
                           sizeof(mlv_diso_hdr_t);

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
    memcpy(ptr, (uint8_t*)&(video->MLVI), sizeof(mlv_file_hdr_t));
    memcpy(ptr += sizeof(mlv_file_hdr_t), (uint8_t*)&(video->RAWI), sizeof(mlv_rawi_hdr_t));
    memcpy(ptr += sizeof(mlv_rawi_hdr_t), (uint8_t*)&(video->RAWC), sizeof(mlv_rawc_hdr_t));
    memcpy(ptr += sizeof(mlv_rawc_hdr_t), (uint8_t*)&(video->IDNT), sizeof(mlv_idnt_hdr_t));
    memcpy(ptr += sizeof(mlv_idnt_hdr_t), (uint8_t*)&(video->EXPO), sizeof(mlv_expo_hdr_t));
    memcpy(ptr += sizeof(mlv_expo_hdr_t), (uint8_t*)&(video->LENS), sizeof(mlv_lens_hdr_t));
    memcpy(ptr += sizeof(mlv_lens_hdr_t), (uint8_t*)&(video->RTCI), sizeof(mlv_rtci_hdr_t));
    memcpy(ptr += sizeof(mlv_rtci_hdr_t), (uint8_t*)&(video->WBAL), sizeof(mlv_wbal_hdr_t));
    memcpy(ptr += sizeof(mlv_wbal_hdr_t), (uint8_t*)&(video->STYL), sizeof(mlv_styl_hdr_t));
    memcpy(ptr += sizeof(mlv_styl_hdr_t), (uint8_t*)&(video->WAVI), sizeof(mlv_wavi_hdr_t));
    memcpy(ptr += sizeof(mlv_wavi_hdr_t), (uint8_t*)&(video->DISO), sizeof(mlv_diso_hdr_t));

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
    const char mapp_ext[6] = { '.', 'M', 'A', 'P', 'P', '\0' };
    memcpy(dot, mapp_ext, 6);

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
            goto mapp_error;
        }

        if ( fread(video->video_index, video_index_size, 1, mappf) != 1 )
        {
            DEBUG( printf("Could not read: %s\n", video->path); )
            goto mapp_error;
        }
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
    ret += fread(&(video->STYL), sizeof(mlv_styl_hdr_t), 1, mappf);
    ret += fread(&(video->WAVI), sizeof(mlv_wavi_hdr_t), 1, mappf);
    ret += fread(&(video->DISO), sizeof(mlv_diso_hdr_t), 1, mappf);
    if(ret != 10)
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

/* Save MLV headers */
int saveMlvHeaders(mlvObject_t * video, FILE * output_mlv, int export_audio, int export_mode, uint32_t frame_start, uint32_t frame_end, const char * version, char * error_message)
{
    if(export_mode == MLV_DF_INT && !video->DARK.blockType[0])
    {
        sprintf(error_message, "There is no internal darkframe in:  %s", video->path);
        DEBUG( printf("\n%s\n", error_message); )
        return 1;
    }

    /* construct version info */
    char version_info[32] = { 0 };
    memcpy(version_info, "MLV App version ", 16);
    strcat(version_info, version);
    size_t vers_info_size = strlen(version_info) + 1;
    size_t vers_block_size = sizeof(mlv_vers_hdr_t) + vers_info_size;
    mlv_vers_hdr_t VERS_HEADER = { "VERS", vers_block_size, 0xFFFFFFFFFFFFFFFF, vers_info_size };

    size_t mlv_headers_size = video->MLVI.blockSize + video->RAWI.blockSize +
                              video->IDNT.blockSize + video->EXPO.blockSize +
                              video->LENS.blockSize + video->WBAL.blockSize +
                              video->RTCI.blockSize + vers_block_size;

    if(video->RAWC.blockType[0]) mlv_headers_size += video->RAWC.blockSize;
    if(video->STYL.blockType[0]) mlv_headers_size += video->STYL.blockSize;
    if(video->DISO.blockType[0]) mlv_headers_size += video->DISO.blockSize;
    if(video->WAVI.blockType[0] && export_audio && export_mode < MLV_AVERAGED_FRAME) mlv_headers_size += video->WAVI.blockSize;
    if(video->INFO.blockType[0] && video->INFO_STRING[0]) mlv_headers_size += video->INFO.blockSize;
    if(video->llrawproc->dark_frame && export_mode < MLV_AVERAGED_FRAME) // if normal MLV export specified and dark frame exists
    {
        df_init(video);
        DEBUG( printf("Block Size = %u, DF Size = %u, Export Mode = %u, Filename = %s\n", video->llrawproc->dark_frame_hdr.blockSize, video->llrawproc->dark_frame_size, export_mode, video->llrawproc->dark_frame_filename); )
        DEBUG( printf("Headers Size += %u\n", video->llrawproc->dark_frame_hdr.blockSize); )
        mlv_headers_size += video->llrawproc->dark_frame_hdr.blockSize;
    }
    uint8_t * mlv_headers_buf = malloc(mlv_headers_size);
    if(!mlv_headers_buf)
    {
        sprintf(error_message, "Could not allocate memory for block headers");
        DEBUG( printf("\n%s\n", error_message); )
        return 1;
    }

    /* fill mlv_headers_buf */
    uint8_t * ptr = mlv_headers_buf;
    mlv_file_hdr_t output_mlvi = { 0 };
    memcpy(&output_mlvi, (uint8_t*)&(video->MLVI), sizeof(mlv_file_hdr_t));
    output_mlvi.fileNum = 0;
    output_mlvi.fileCount = 1;
    output_mlvi.videoFrameCount = (export_mode >= MLV_AVERAGED_FRAME) ? 1 : frame_end - frame_start + 1;
    output_mlvi.audioFrameCount = (!export_audio || export_mode >= MLV_AVERAGED_FRAME) ? 0 : 1;
    if(export_mode == MLV_COMPRESSED && (!isMlvCompressed(video))) output_mlvi.videoClass |= MLV_VIDEO_CLASS_FLAG_LJ92;
    if(export_mode >= MLV_AVERAGED_FRAME && isMlvCompressed(video)) output_mlvi.videoClass = 1;
    output_mlvi.audioClass = (!export_audio || export_mode >= MLV_AVERAGED_FRAME) ? 0 : 1;
    if(export_mode == MLV_DF_INT)
    {
        output_mlvi.sourceFpsNom = video->DARK.sourceFpsNom;
        output_mlvi.sourceFpsDenom = video->DARK.sourceFpsDenom;
    }
    memcpy(ptr, &output_mlvi, sizeof(mlv_file_hdr_t));
    ptr += video->MLVI.blockSize;

    if(export_mode == MLV_DF_INT)
    {
        mlv_rawi_hdr_t output_rawi = { 0 };
        memcpy(&output_rawi, (uint8_t*)&(video->RAWI), sizeof(mlv_rawi_hdr_t));
        output_rawi.xRes = video->DARK.xRes;
        output_rawi.yRes = video->DARK.yRes;
        output_rawi.raw_info.width = video->DARK.rawWidth;
        output_rawi.raw_info.height = video->DARK.rawHeight;
        output_rawi.raw_info.bits_per_pixel = video->DARK.bits_per_pixel;
        output_rawi.raw_info.black_level = video->DARK.black_level;
        output_rawi.raw_info.white_level = video->DARK.white_level;
        memcpy(ptr, &output_rawi, sizeof(mlv_rawi_hdr_t));
    }
    else
    {
        memcpy(ptr, (uint8_t*)&(video->RAWI), sizeof(mlv_rawi_hdr_t));
    }
    ptr += video->RAWI.blockSize;

    if(video->RAWC.blockType[0])
    {
        if(export_mode == MLV_DF_INT)
        {
            mlv_rawc_hdr_t output_rawc = { 0 };
            memcpy(&output_rawc, (uint8_t*)&(video->RAWC), sizeof(mlv_rawc_hdr_t));
            output_rawc.binning_x = video->DARK.binning_x;
            output_rawc.skipping_x = video->DARK.skipping_x;
            output_rawc.binning_y = video->DARK.binning_y;
            output_rawc.skipping_y = video->DARK.skipping_y;
            memcpy(ptr, &output_rawc, sizeof(mlv_rawc_hdr_t));
        }
        else
        {
            memcpy(ptr, (uint8_t*)&(video->RAWC), sizeof(mlv_rawc_hdr_t));
        }
        ptr += video->RAWC.blockSize;
    }

    if(export_mode == MLV_DF_INT)
    {
        mlv_idnt_hdr_t output_idnt = { 0 };
        memcpy(&output_idnt, (uint8_t*)&(video->IDNT), sizeof(mlv_idnt_hdr_t));
        output_idnt.cameraModel = video->DARK.cameraModel;
        memcpy(ptr, &output_idnt, sizeof(mlv_idnt_hdr_t));
    }
    else
    {
        memcpy(ptr, (uint8_t*)&(video->IDNT), sizeof(mlv_idnt_hdr_t));
    }
    ptr += video->IDNT.blockSize;

    if(export_mode == MLV_DF_INT)
    {
        mlv_expo_hdr_t output_expo = { 0 };
        memcpy(&output_expo, (uint8_t*)&(video->EXPO), sizeof(mlv_expo_hdr_t));
        output_expo.isoMode = video->DARK.isoMode;
        output_expo.isoValue = video->DARK.isoValue;
        output_expo.isoAnalog = video->DARK.isoAnalog;
        output_expo.digitalGain = video->DARK.digitalGain;
        output_expo.shutterValue = video->DARK.shutterValue;
        memcpy(ptr, &output_expo, sizeof(mlv_expo_hdr_t));
    }
    else
    {
        memcpy(ptr, (uint8_t*)&(video->EXPO), sizeof(mlv_expo_hdr_t));
    }
    ptr += video->EXPO.blockSize;

    memcpy(ptr, (uint8_t*)&(video->LENS), sizeof(mlv_lens_hdr_t));
    ptr += video->LENS.blockSize;

    memcpy(ptr, (uint8_t*)&(video->WBAL), sizeof(mlv_wbal_hdr_t));
    ptr += video->WBAL.blockSize;

    if(video->STYL.blockType[0])
    {
        memcpy(ptr, (uint8_t*)&(video->STYL), sizeof(mlv_styl_hdr_t));
        ptr += video->STYL.blockSize;
    }

    memcpy(ptr, (uint8_t*)&(video->RTCI), sizeof(mlv_rtci_hdr_t));
    ptr += video->RTCI.blockSize;

    if(video->INFO.blockType[0] && video->INFO_STRING[0])
    {
        memcpy(ptr, (uint8_t*)&(video->INFO), sizeof(mlv_info_hdr_t));
        ptr += sizeof(mlv_info_hdr_t);
        memcpy(ptr, (uint8_t*)&(video->INFO_STRING), strlen(video->INFO_STRING) + 1);
        ptr += (video->INFO.blockSize - sizeof(mlv_info_hdr_t) + strlen(video->INFO_STRING) + 1);
    }

    if(video->DISO.blockType[0])
    {
        memcpy(ptr, (uint8_t*)&(video->DISO), sizeof(mlv_diso_hdr_t));
        ptr += video->DISO.blockSize;
    }

    memcpy(ptr, &VERS_HEADER, sizeof(mlv_vers_hdr_t));
    ptr += sizeof(mlv_vers_hdr_t);
    memcpy(ptr, version_info, vers_info_size);
    ptr += vers_info_size;

    if(video->WAVI.blockType[0] && export_audio && (export_mode < MLV_AVERAGED_FRAME))
    {
        memcpy(ptr, (uint8_t*)&(video->WAVI), sizeof(mlv_wavi_hdr_t));
        ptr += video->WAVI.blockSize;
    }

    if(video->llrawproc->dark_frame && export_mode < MLV_AVERAGED_FRAME) // if normal MLV export specified and dark frame exists
    {
        memcpy(ptr, (uint8_t*)&(video->llrawproc->dark_frame_hdr), sizeof(mlv_dark_hdr_t));
        ptr += sizeof(mlv_dark_hdr_t);

        size_t df_packed_size = video->llrawproc->dark_frame_hdr.blockSize - sizeof(mlv_dark_hdr_t);
        uint8_t * df_packed = calloc(df_packed_size, 1);
        dng_pack_image_bits((uint16_t *)df_packed, video->llrawproc->dark_frame_data, video->llrawproc->dark_frame_hdr.xRes, video->llrawproc->dark_frame_hdr.yRes, video->llrawproc->dark_frame_hdr.bits_per_pixel, 0);
        memcpy(ptr, df_packed, df_packed_size);
        ptr += df_packed_size;
        DEBUG( printf("\nDARK block inserted\n"); )
    }

    /* write mlv_headers_buf */
    if(fwrite(mlv_headers_buf, mlv_headers_size, 1, output_mlv) != 1)
    {
        sprintf(error_message, "Could not write MLV headers");
        DEBUG( printf("\n%s\n", error_message); )
        free(mlv_headers_buf);
        return 1;
    }

    DEBUG( printf("\nMLV headers saved\n"); )
    free(mlv_headers_buf);
    return 0;
}

/* Save video frame plus audio if available */
int saveMlvAVFrame(mlvObject_t * video, FILE * output_mlv, int export_audio, int export_mode, uint32_t frame_start, uint32_t frame_end, uint32_t frame_index, uint32_t * avg_buf, char * error_message)
{
    mlv_vidf_hdr_t vidf_hdr = { 0 };

    int write_ok = (export_mode == 2) ? 0 : 1;
    uint32_t pixel_count = video->RAWI.xRes * video->RAWI.yRes;
    uint32_t frame_size_packed = (uint32_t)(pixel_count * video->RAWI.raw_info.bits_per_pixel / 8);
    uint32_t frame_size_unpacked = pixel_count * 2;
    uint32_t max_frame_number = frame_end - frame_start + 1;

    int chunk = video->video_index[frame_index].chunk_num;
    uint32_t frame_size = video->video_index[frame_index].frame_size;
    uint64_t frame_offset = video->video_index[frame_index].frame_offset;
    uint64_t block_offset = video->video_index[frame_index].block_offset;

    /* read VIDF block header */
    file_set_pos(video->file[chunk], block_offset, SEEK_SET);
    if(fread(&vidf_hdr, sizeof(mlv_vidf_hdr_t), 1, video->file[chunk]) != 1)
    {
        sprintf(error_message, "Could not read VIDF block header from:  %s", video->path);
        DEBUG( printf("\n%s\n", error_message); )
        return 1;
    }

    vidf_hdr.blockSize -= vidf_hdr.frameSpace;
    vidf_hdr.frameSpace = 0;

    /* for safety allocate max possible size buffer for VIDF block, calculated for 16bits per pixel */
    uint8_t * block_buf = calloc(sizeof(mlv_vidf_hdr_t) + frame_size_unpacked, 1);
    if(!block_buf)
    {
        sprintf(error_message, "Could not allocate memory for VIDF block");
        DEBUG( printf("\n%s\n", error_message); )
        return 1;
    }
    /* for safety allocate max possible size buffer for image data, calculated for 16bits per pixel */
    uint8_t * frame_buf = calloc(frame_size_unpacked, 1);
    if(!frame_buf)
    {
        sprintf(error_message, "Could not allocate memory for VIDF frame");
        DEBUG( printf("\n%s\n", error_message); )
        free(block_buf);
        return 1;
    }

    /* read frame buffer */
    file_set_pos(video->file[chunk], frame_offset, SEEK_SET);
    if(fread(frame_buf, frame_size, 1, video->file[chunk]) != 1)
    {
        sprintf(error_message, "Could not read VIDF image data from:  %s", video->path);
        DEBUG( printf("\n%s\n", error_message); )
        free(frame_buf);
        free(block_buf);
        return 1;
    }

    if(export_mode == MLV_DF_INT) // export internal dark frame as separate MLV
    {
        size_t df_packed_size = video->DARK.blockSize - sizeof(mlv_dark_hdr_t);
        /* read dark frame */
        file_set_pos(video->file[0], video->dark_frame_offset, SEEK_SET);
        if(fread(frame_buf, df_packed_size, 1, video->file[0]) != 1)
        {
            sprintf(error_message, "Could not read DARK block image data from:  %s", video->path);
            DEBUG( printf("\n%s\n", error_message); )
            free(frame_buf);
            free(block_buf);
            return 1;
        }
        /* set blocksize and samplesAveraged to frameNumber */
        vidf_hdr.blockSize = video->DARK.blockSize;
        vidf_hdr.frameNumber = video->DARK.samplesAveraged;
        memcpy(block_buf, &vidf_hdr, sizeof(mlv_vidf_hdr_t));
        memcpy((block_buf + sizeof(mlv_vidf_hdr_t)), frame_buf, df_packed_size);
    }
    else if(export_mode == MLV_AVERAGED_FRAME) // average all frames to one dark frame
    {
        uint16_t * frame_buf_unpacked = calloc(frame_size_unpacked, 1);
        if(!frame_buf_unpacked)
        {
            sprintf(error_message, "Averaging: could not allocate memory for unpacked frame");
            DEBUG( printf("\n%s\n", error_message); )
            free(frame_buf);
            free(block_buf);
            return 1;
        }
        if(isMlvCompressed(video))
        {
            int ret = dng_decompress_image(frame_buf_unpacked, (uint16_t*)frame_buf, frame_size, video->RAWI.xRes, video->RAWI.yRes, video->RAWI.raw_info.bits_per_pixel);
            if(ret != LJ92_ERROR_NONE)
            {
                sprintf(error_message, "Averaging: could not decompress frame:  LJ92_ERROR %u", ret);
                DEBUG( printf("\n%s\n", error_message); )
                free(frame_buf_unpacked);
                free(frame_buf);
                free(block_buf);
                return ret;
            }
        }
        else
        {
            dng_unpack_image_bits(frame_buf_unpacked, (uint16_t*)frame_buf, video->RAWI.xRes, video->RAWI.yRes, video->RAWI.raw_info.bits_per_pixel);
        }
        for(uint32_t i = 0; i < pixel_count; i++)
        {
            avg_buf[i] += frame_buf_unpacked[i];
        }

        if(frame_index == frame_end - 1)
        {
            for(uint32_t i = 0; i < pixel_count; i++)
            {
                frame_buf_unpacked[i] = (avg_buf[i] + max_frame_number / 2) / max_frame_number;
            }
            dng_pack_image_bits((uint16_t *)frame_buf, frame_buf_unpacked, video->RAWI.xRes, video->RAWI.yRes, video->RAWI.raw_info.bits_per_pixel, 0);

            vidf_hdr.frameNumber = max_frame_number;
            vidf_hdr.blockSize = sizeof(mlv_vidf_hdr_t) + frame_size_packed;
            memcpy(block_buf, &vidf_hdr, sizeof(mlv_vidf_hdr_t));
            memcpy((block_buf + sizeof(mlv_vidf_hdr_t)), frame_buf, frame_size_packed);
            write_ok = 1;
        }

        free(frame_buf_unpacked);
    }
    else if((export_mode == MLV_COMPRESSED) && (!isMlvCompressed(video))) // compress MLV frame with LJ92 if specified
    {
        int ret = 0;
        size_t frame_size_compressed = 0;

        uint16_t * frame_buf_unpacked = calloc(frame_size_unpacked, 1);
        uint16_t * frame_buf_compressed = calloc(frame_size_unpacked, 1);
        if(!frame_buf_unpacked || !frame_buf_compressed)
        {
            DEBUG( printf("\nCould not allocate memory for frame compressing\n"); )
            ret = 1;
        }

        if(!ret)
        {
            dng_unpack_image_bits(frame_buf_unpacked, (uint16_t*)frame_buf, video->RAWI.xRes, video->RAWI.yRes, video->RAWI.raw_info.bits_per_pixel);
            ret = dng_compress_image(frame_buf_compressed, frame_buf_unpacked, &frame_size_compressed, video->RAWI.xRes, video->RAWI.yRes, video->RAWI.raw_info.bits_per_pixel);
            if(ret == LJ92_ERROR_NONE)
            {
                vidf_hdr.blockSize = sizeof(mlv_vidf_hdr_t) + frame_size_compressed;
                memcpy(block_buf, &vidf_hdr, sizeof(mlv_vidf_hdr_t));
                memcpy((block_buf + sizeof(mlv_vidf_hdr_t)), (uint8_t*)frame_buf_compressed, frame_size_compressed);
            }
            else // if compression error then save uncompressed raw
            {
                memcpy(block_buf, &vidf_hdr, sizeof(mlv_vidf_hdr_t));
                memcpy((block_buf + sizeof(mlv_vidf_hdr_t)), frame_buf, frame_size);

                /* patch MLVI header and set back videoClass to 1 (uncompressed) */
                uint64_t current_pos = file_get_pos(output_mlv);
                file_set_pos(output_mlv, 32, SEEK_SET);
                uint16_t videoClass = 0x1;
                if(fwrite(&videoClass, sizeof(uint16_t), 1, output_mlv) != 1)
                {
                    DEBUG( printf("\nCould not patch videoClass in MLV header\n"); )
                }
                file_set_pos(output_mlv, current_pos, SEEK_SET);
            }
        }

        if(frame_buf_unpacked) free(frame_buf_unpacked);
        if(frame_buf_compressed) free(frame_buf_compressed);
    }
    else // pass through the original raw frame
    {
        memcpy(block_buf, &vidf_hdr, sizeof(mlv_vidf_hdr_t));
        memcpy((block_buf + sizeof(mlv_vidf_hdr_t)), frame_buf, frame_size);
    }

    /* if audio export is enabled */
    if(!(frame_start - frame_index - 1) && export_audio && export_mode < MLV_AVERAGED_FRAME )
    {
        /* Get MLV audio data into buffer */
        uint64_t mlv_audio_size = 0;
        int16_t * mlv_audio_data = (int16_t*)getMlvAudioData(video, &mlv_audio_size);
        if(!mlv_audio_data)
        {
            sprintf(error_message, "Could not allocate memory for audio data");
            DEBUG( printf("\n%s\n", error_message); )
            free(frame_buf);
            free(block_buf);
            return 1;
        }

        /* initialize AUDF header */
        mlv_audf_hdr_t audf_hdr = { { 'A','U','D','F' }, 0, 0, 0, 0 };

        /* Calculate the sum of audio sample sizes for all audio channels */
        uint64_t audio_sample_size = getMlvAudioChannels(video) * (getMlvAudioBitsPerSample(video) / 8);
        /* Calculate audio starting offset */
        uint64_t audio_start_offset = ( (uint64_t)( (double)(getMlvSampleRate(video) * audio_sample_size * (frame_start - 1)) / (double)getMlvFramerate(video) ) );
        /* Make sure start offset value is multiple of sum of all channel sample sizes */
        uint64_t audio_start_offset_aligned = audio_start_offset - (audio_start_offset % audio_sample_size);
        /* Calculate cut audio size */
        uint64_t cut_audio_size = (uint64_t)( (double)(getMlvSampleRate(video) * audio_sample_size * (frame_end - frame_start + 1)) / (double)getMlvFramerate(video) );
        /* Calculate the audio alignement block size in bytes */
        uint16_t block_align = getMlvAudioChannels(video) * getMlvAudioBitsPerSample(video) * 1024 / 8;
        /* check if cut_audio_size is multiple of 'block_align' bytes and not more than original audio data size */
        uint64_t cut_audio_size_aligned = MIN( (cut_audio_size - (cut_audio_size % block_align) + block_align), mlv_audio_size );
        /* make max audio size (uint32_t max value - 1) multiple of 'block_align' bytes */
        uint32_t max_audio_size = 0xFFFFFFFF - (0xFFFFFFFF % block_align);
        /* Not likely that audio size exeeds the 4.3gb but anyway check if cut_audio_size is more than uint32_t max value to not overflow blockSize variable */
        if(cut_audio_size_aligned > max_audio_size) cut_audio_size_aligned = max_audio_size;

        /* fill AUDF block header */
        audf_hdr.blockSize = sizeof(mlv_audf_hdr_t) + cut_audio_size_aligned;
        audf_hdr.timestamp = vidf_hdr.timestamp;

        /* write AUDF block header */
        if(fwrite(&audf_hdr, sizeof(mlv_audf_hdr_t), 1, output_mlv) != 1)
        {
            sprintf(error_message, "Could not write AUDF block header");
            DEBUG( printf("\n%s\n", error_message); )
            free(mlv_audio_data);
            free(frame_buf);
            free(block_buf);
            return 1;
        }

        /* write audio data */
        if(fwrite((uint8_t *)mlv_audio_data + audio_start_offset_aligned, cut_audio_size_aligned, 1, output_mlv) != 1)
        {
            sprintf(error_message, "Could not write AUDF block audio data");
            DEBUG( printf("\n%s\n", error_message); )
            free(mlv_audio_data);
            free(frame_buf);
            free(block_buf);
            return 1;
        }

        free(mlv_audio_data);
    }

    /* write mlvFrame */
    if(write_ok)
    {
        if(fwrite(block_buf, vidf_hdr.blockSize, 1, output_mlv) != 1)
        {
            sprintf(error_message, "Could not write video frame #%u", frame_index);
            DEBUG( printf("\n%s\n", error_message); )
            free(frame_buf);
            free(block_buf);
            return 1;
        }
    }

    free(frame_buf);
    free(block_buf);
    DEBUG( if( (export_mode == MLV_FAST_PASS) && (!isMlvCompressed(video)) ) printf("Saved video frame #%u\n", frame_index); )
    return 0;
}

/* Reads an MLV file in to a mlv object(mlvObject_t struct) 
 * only puts metadata in to the mlvObject_t, 
 * no debayering or bit unpacking */
int openMlvClip(mlvObject_t * video, char * mlvPath, int open_mode, char * error_message)
{
    video->path = malloc( strlen(mlvPath) + 1 );
    memcpy(video->path, mlvPath, strlen(mlvPath));
    video->path[strlen(mlvPath)] = 0x0;
    video->file = load_all_chunks(mlvPath, &video->filenum);
    if(!video->file)
    {
        sprintf(error_message, "Could not open file:  %s", video->path);
        DEBUG( printf("\n%s\n", error_message); )
        return MLV_ERR_OPEN; // can not open file
    }

    int block_num = 0; /* Number of blocks in file */

    if(!load_mapp(video)) goto short_cut;

    mlv_hdr_t block_header; /* Basic MLV block header */
    uint64_t video_frames = 0; /* Number of frames in video */
    uint64_t audio_frames = 0; /* Number of audio blocks in video */
    uint64_t video_index_max = 0; /* initial size of frame index */
    uint64_t audio_index_max = 0; /* initial size of audio index */
    int rtci_read = 0; /* Flips to 1 if 1st RTCI block was read */
    int lens_read = 0; /* Flips to 1 if 1st LENS block was read */
    int wbal_read = 0; /* Flips to 1 if 1st WBAL block was read */
    int styl_read = 0; /* Flips to 1 if 1st STYL block was read */
    int fread_err = 1;

    for(int i = 0; i < video->filenum; i++)
    {
        /* Getting size of file in bytes */
        file_set_pos(video->file[i], 0, SEEK_END);
        uint64_t file_size = file_get_pos(video->file[i]);
        if ( !file_size )
        {
            sprintf(error_message, "Zero byte size file:  %s", video->path);
            DEBUG( printf("\n%s\n", error_message); )
            --video->filenum;
            return MLV_ERR_INVALID;
        }
        file_set_pos(video->file[i], 0, SEEK_SET); /* Start of file */

        /* Read file header */
        if ( fread(&block_header, sizeof(mlv_hdr_t), 1, video->file[i]) != 1 )
        {
            sprintf(error_message, "File is too short to be a valid MLV:  %s", video->path);
            DEBUG( printf("\n%s\n", error_message); )
            --video->filenum;
            return MLV_ERR_INVALID;
        }
        file_set_pos(video->file[i], 0, SEEK_SET); /* Start of file */

        if ( memcmp(block_header.blockType, "MLVI", 4) == 0 )
        {
            fread_err &= fread(&video->MLVI, sizeof(mlv_file_hdr_t), 1, video->file[i]);
        }
        else
        {
            sprintf(error_message, "File header is missing, invalid MLV:  %s", video->path);
            DEBUG( printf("\n%s\n", error_message); )
            --video->filenum;
            return MLV_ERR_INVALID;
        }

        while ( file_get_pos(video->file[i]) < file_size ) /* Check if were at end of file yet */
        {
            /* Record position to go back to it later if block is read */
            uint64_t block_start = file_get_pos(video->file[i]);
            /* Read block header */
            fread_err &= fread(&block_header, sizeof(mlv_hdr_t), 1, video->file[i]);
            if(block_header.blockSize < sizeof(mlv_hdr_t))
            {
                sprintf(error_message, "Invalid blockSize '%u', corrupted file:  %s", block_header.blockSize, video->path);
                DEBUG( printf("\n%s\n", error_message); )
                --video->filenum;
                return MLV_ERR_INVALID;
            }

            /* Next block location */
            uint64_t next_block = (uint64_t)block_start + (uint64_t)block_header.blockSize;
            /* Go back to start of block for next bit */
            file_set_pos(video->file[i], block_start, SEEK_SET);

            /* Now check what kind of block it is and read it in to the mlv object */
            if ( memcmp(block_header.blockType, "NULL", 4) == 0 || memcmp(block_header.blockType, "BKUP", 4) == 0)
            {
                /* do nothing, skip this block */
            }
            else if ( memcmp(block_header.blockType, "VIDF", 4) == 0 )
            {
                fread_err &= fread(&video->VIDF, sizeof(mlv_vidf_hdr_t), 1, video->file[i]);

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
                video->video_index[video_frames].frame_number = video->VIDF.frameNumber;
                video->video_index[video_frames].frame_time = video->VIDF.timestamp;
                video->video_index[video_frames].block_offset = block_start;

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
                fread_err &= fread(&video->AUDF, sizeof(mlv_audf_hdr_t), 1, video->file[i]);

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
                video->audio_index[audio_frames].frame_number = video->AUDF.frameNumber;
                video->audio_index[audio_frames].frame_time = video->AUDF.timestamp;
                video->audio_index[audio_frames].block_offset = block_start;

                /* Count actual audio frames */
                audio_frames++;
            }
            else if ( memcmp(block_header.blockType, "RAWI", 4) == 0 )
            {
                fread_err &= fread(&video->RAWI, sizeof(mlv_rawi_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "RAWC", 4) == 0 )
            {
                fread_err &= fread(&video->RAWC, sizeof(mlv_rawc_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "WAVI", 4) == 0 )
            {
                fread_err &= fread(&video->WAVI, sizeof(mlv_wavi_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "EXPO", 4) == 0 )
            {
                fread_err &= fread(&video->EXPO, sizeof(mlv_expo_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "LENS", 4) == 0 )
            {
                if( !lens_read )
                {
                    fread_err &= fread(&video->LENS, sizeof(mlv_lens_hdr_t), 1, video->file[i]);
                    lens_read = 1; //read only first one
                }
            }
            else if ( memcmp(block_header.blockType, "WBAL", 4) == 0 )
            {
                if( !wbal_read )
                {
                    fread_err &= fread(&video->WBAL, sizeof(mlv_wbal_hdr_t), 1, video->file[i]);
                    wbal_read = 1; //read only first one
                }
            }
            else if ( memcmp(block_header.blockType, "STYL", 4) == 0 )
            {
                if( !styl_read )
                {
                    fread_err &= fread(&video->STYL, sizeof(mlv_styl_hdr_t), 1, video->file[i]);
                    styl_read = 1; //read only first one
                }
            }
            else if ( memcmp(block_header.blockType, "RTCI", 4) == 0 )
            {
                if( !rtci_read )
                {
                    fread_err &= fread(&video->RTCI, sizeof(mlv_rtci_hdr_t), 1, video->file[i]);
                    rtci_read = 1; //read only first one
                }
            }
            else if ( memcmp(block_header.blockType, "IDNT", 4) == 0 )
            {
                fread_err &= fread(&video->IDNT, sizeof(mlv_idnt_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "INFO", 4) == 0 )
            {
                fread_err &= fread(&video->INFO, sizeof(mlv_info_hdr_t), 1, video->file[i]);
                if(video->INFO.blockSize > sizeof(mlv_info_hdr_t))
                {
                    fread_err &= fread(&video->INFO_STRING, video->INFO.blockSize - sizeof(mlv_info_hdr_t), 1, video->file[i]);
                }
            }
            else if ( memcmp(block_header.blockType, "DISO", 4) == 0 )
            {
                fread_err &= fread(&video->DISO, sizeof(mlv_diso_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "MARK", 4) == 0 )
            {
                /* do nothing atm */
                //fread(&video->MARK, sizeof(mlv_mark_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "ELVL", 4) == 0 )
            {
                /* do nothing atm */
                //fread(&video->ELVL, sizeof(mlv_elvl_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "DEBG", 4) == 0 )
            {
                /* do nothing atm */
                //fread(&video->DEBG, sizeof(mlv_debg_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "VERS", 4) == 0 )
            {
                /* do nothing atm */
                //fread(&video->VERS, sizeof(mlv_vers_hdr_t), 1, video->file[i]);
            }
            else if ( memcmp(block_header.blockType, "DARK", 4) == 0 )
            {
                fread_err &= fread(&video->DARK, sizeof(mlv_dark_hdr_t), 1, video->file[i]);
                video->dark_frame_offset = file_get_pos(video->file[i]);
            }
            else
            {
                char block_type[5] = { 0 };
                memcpy(block_type, block_header.blockType, 4);
                sprintf(error_message, "Unknown blockType '%s' or corrupted file:  %s", block_type, video->path);
                DEBUG( printf("\n%s\n", error_message); )
                --video->filenum;
                return MLV_ERR_INVALID;
            }

            /* Printing stuff for fun */
            //DEBUG( printf("Block #%4i  |  %.4s  |%9i Bytes\n", block_num, block_header.blockType, block_header.blockSize); )

            /* Move to next block */
            file_set_pos(video->file[i], next_block, SEEK_SET);

            block_num++;
        }
    }

    /* Return with error if no video frames found */
    if(!fread_err)
    {
        sprintf(error_message, "File read error:  %s", video->path);
        DEBUG( printf("\n%s\n", error_message); )
        --video->filenum;
        return MLV_ERR_IO;
    }
    /* Return with error if no video frames found */
    if(!video_frames)
    {
        sprintf(error_message, "No video frames found in:  %s", video->path);
        DEBUG( printf("\n%s\n", error_message); )
        --video->filenum;
        return MLV_ERR_INVALID;
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

    /* Calculate imaginary bit depth for restricted lossledd raw data */
    video->lossless_bpp = ceil( log2( video->RAWI.raw_info.white_level - video->RAWI.raw_info.black_level ) );

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

/* Get image aspect ratio according to RAWC block info, calculating from binnin + skipping values.
   Returns aspect ratio or 0 in case if RAWC block is not present in MLV file */
float getMlvAspectRatio(mlvObject_t * video)
{
    if(video->RAWC.blockType[0])
    {
        int sampling_x = video->RAWC.binning_x + video->RAWC.skipping_x;
        int sampling_y = video->RAWC.binning_y + video->RAWC.skipping_y;

        if( sampling_x == 0 ) return 0;
        return ( (float)sampling_y / (float)sampling_x );
    }
    return 0;
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
