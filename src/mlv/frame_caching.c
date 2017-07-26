/* Yeas, we have another background thread */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "video_mlv.h"
#include "../debayer/debayer.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

/* Hmmmm, did anyone need 2 ways of doing this? */

/* What I call MegaBytes is actually MebiBytes! I'm so upset to find that out :( */
void setMlvRawCacheLimitMegaBytes(mlvObject_t * video, uint64_t megaByteLimit)
{
    uint64_t frame_size  = getMlvWidth(video) * getMlvHeight(video) * sizeof(uint16_t) * 3;
    uint64_t bytes_limit = megaByteLimit * (1 << 20);

    video->cache_limit_mb = megaByteLimit;
    video->cache_limit_bytes = bytes_limit;


    /* Protection against zero division, cuz that causes "Floating point exception: 8"... 
     * ...LOL there's not even a floating point in sight */
    if (isMlvActive(video) && frame_size != 0)
    {
        uint64_t frame_limit = (uint64_t)bytes_limit / (uint64_t)frame_size;
        uint64_t cache_whole = frame_size * getMlvFrames(video);

        video->cache_limit_frames = frame_limit;

        printf("\nEnough memory allowed to cache %i frames (%i MiB)\n\n", (int)frame_limit, (int)megaByteLimit);

        /* Resize cache block - to maximum allowed or enough to fit whole clip if it is smaller */
        video->cache_memory_block = realloc(video->cache_memory_block, MIN(bytes_limit, cache_whole));

        /* Begin updating cached frames */
        if (!video->is_caching)
        {
            pthread_create(&video->cache_thread, NULL, (void *)cache_mlv_frames, (void *)video);
        }
    }

    /* No else - if video is not active we won't waste RAM */
}

/* Not recommended */
void setMlvRawCacheLimitFrames(mlvObject_t * video, uint64_t frameLimit)
{
    uint64_t frame_size = getMlvWidth(video) * getMlvHeight(video) * sizeof(uint16_t) * 3;

    /* Do only if clip is loaded */
    if (isMlvActive(video) && frame_size != 0)
    {
        uint64_t bytes_limit = frame_size * frameLimit;
        uint64_t mbyte_limit = bytes_limit / (1 << 20);
        uint64_t cache_whole = frame_size * getMlvFrames(video);

        video->cache_limit_bytes = bytes_limit;
        video->cache_limit_mb = mbyte_limit;
        video->cache_limit_frames = frameLimit;

        /* Resize cache block - to maximum allowed or enough to fit whole clip if it is smaller */
        video->cache_memory_block = realloc(video->cache_memory_block, MIN(bytes_limit, cache_whole));

        /* Begin updating cached frames */
        if (!video->is_caching)
        {
            /* cache on bg thread */
            pthread_create(&video->cache_thread, NULL, (void *)cache_mlv_frames, (void *)video);
        }
    }
}

/* Will run in background, caching all frames until it is done,
 * and will be called again on a change */
/* TODO: add removing old/un-needed frames ability */
void cache_mlv_frames(mlvObject_t * video)
{
    video->is_caching = 1;

    uint32_t width = getMlvWidth(video);
    uint32_t height = getMlvHeight(video);
    uint32_t cache_frames = MIN((int)video->cache_limit_frames, (int)video->frames);
    uint32_t frame_size_rgb = width * height * 3;

    float * raw_frame = malloc( getMlvWidth(video) * getMlvHeight(video) * sizeof(float) );

    printf("\nTotal frames %i, Cache limit frames: %i\n\n", (int)video->frames, (int)video->cache_limit_frames);

    /* Cache until done */
    for (uint32_t frame_index = 0; frame_index < cache_frames; ++frame_index)
    {
        /* Only debayer if frame is not already cached and has not been requested to stop */
        if (!video->cached_frames[frame_index] && !video->stop_caching)
        {
            video->currently_caching = frame_index;

            /* Use memory within our block */
            video->rgb_raw_frames[frame_index] = video->cache_memory_block + (frame_size_rgb * frame_index);

            /* debayer_type 1, we want to cache AMaZE frames */
            get_mlv_raw_frame_debayered(video, frame_index, raw_frame, video->rgb_raw_frames[frame_index], 1);

            video->cached_frames[frame_index] = 1;

            printf("Debayered frame %i/%i has been cached.\n", frame_index, cache_frames);
        }
    }

    free(raw_frame);

    video->is_caching = 0;
}

/* Gets a freshly debayered frame every time ( temp memory should be Width * Height * sizeof(float) ) */
void get_mlv_raw_frame_debayered( mlvObject_t * video, 
                                  int frame_index, 
                                  float * temp_memory, 
                                  uint16_t * output_frame, 
                                  int debayer_type ) /* 0=bilinear 1=amaze */
{
    int width = getMlvWidth(video);
    int height = getMlvHeight(video);

    /* Get the raw data in B&W */
    getMlvRawFrameFloat(video, frame_index, temp_memory);

    if (debayer_type)
    {
        /* Debayer AMAZEly - using all cores! */
        debayerAmaze(output_frame, temp_memory, width, height, getMlvCpuCores(video));
    }
    else
    {
        /* Debayer quickly (bilinearly) */
        debayerBasic(output_frame, temp_memory, width, height, 1);
    }
}
