/* Functions that render MLV frames in the background,
 * updating the view */

#import <Cocoa/Cocoa.h>

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#include "background_thread.h"

#include "../../src/mlv_include.h"

/* What frame we r on */
extern int currentFrameIndex;
/* ++ this on changes such as settings adjustments or on playback to go to next frame */
extern int frameChanged;
/* Used to stop drawing */
extern int dontDraw;
/* Main thing */
extern mlvObject_t * videoMLV;
/* Holds rawBitmap inside it */
extern NSImage * rawImageObject;
/* Holds a (the) processed frame that is displayed
 * Will be changed with methods from this file */
extern NSBitmapImageRep * rawBitmap;
/* The image data of ^^^^^^^^^^^^^^^ */
extern uint8_t * rawImage;
/* This is the view, so we can refresh it
 * by doing: 
 *     [previewWindow setImage: tempImage]; // NULL or nil causes crash of OS
 *     [previewWindow setImage: rawImageObject]; 
 * Sets an empty image, then back to the proper one */
extern NSImageView * previewWindow;

/* App window */
extern NSWindow * window;

/* The threads */
static pthread_t timing_thread;
static pthread_t draw_thread;
/* Delay variable (start with a ok value) */
static int delayU = 1000000;
/* Not to draw frames at once */
static int frame_still_drawing = 0;


/* yh whatever */
void beginFrameDrawing()
{
    /* Create a background thread with framerate_timer */
    pthread_create(&timing_thread, NULL, (void *)framerate_timer, NULL);
}


/* Goes round and round at framerate and renders a frame on every tick
 * of the frame clock - if the previous frame is not still rendering */
void framerate_timer()
{
    /* Never stop! */
    while (1 < 2)
    {
        /* Delay in usecs or somehting, recalculated in case of a user change.
         * Also minus 20, just to compensate for all the stuff idk how long it all takes */
        delayU = (int) (1000000.0f / getMlvFramerate(videoMLV) - 20.0f);

        /* Don't allow frame rates(refresh rates) above 60fps ish */
        if (delayU < 16000.0f)
        {
            delayU = 16000.0f;
        }

        /* wait for that amount of time */
        usleep(delayU);

        /* If frame is not still drawing and needs updating, draw again */
        if (!frame_still_drawing && frameChanged && !dontDraw)
        {
            /* I don't care if this is inefficient */
            pthread_create(&draw_thread, NULL, (void *)draw_frame, NULL);
            /* Reset this */
            frameChanged = 0;
        }
    }
}


/* Draws/updates frame */
void draw_frame()
{
    /* The frame is drawing! */
    frame_still_drawing = 1;

    /* Draw frame and update view now */

    /* Get dhe frame(8 bit cos most screens are) */
    getMlvProcessedFrame8(videoMLV, currentFrameIndex, rawImage);

    /* Update/refresh the view... this seems to be the buggiest part of the 
     * whole program, please hlp make better playback if you know how to do that */
    [previewWindow setImage: nil];
    [previewWindow setImage: rawImageObject];

    /* Doesn't seem to help the issues with old images remaining (not my bugs!) */
    [window update];

    /* Reset, we don't want to stop */
    frame_still_drawing = 0;
}