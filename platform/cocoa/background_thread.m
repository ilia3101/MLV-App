/* Functions that render MLV frames in the background,
 * updating the view */

#import <Cocoa/Cocoa.h>

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#include "mac_info.h"

#include "main_methods.h"

#include "background_thread.h"

#include "../../src/mlv_include.h"

/* God object used to share globals (type) */
#include "godobject.h"
/* The godobject itsself */
extern godObject_t * App;


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
        delayU = (int) (1000000.0f / getMlvFramerate(App->videoMLV) - 20.0f);

        /* Don't allow frame rates(refresh rates) above 60fps ish */
        if (delayU < 16000.0f)
        {
            delayU = 16000.0f;
        }

        /* wait for that amount of time */
        usleep(delayU);

        /* If frame is not still drawing and needs updating, draw again */
        if (!frame_still_drawing && App->frameChanged && !App->dontDraw)
        {
            /* I don't care if this is inefficient */
            pthread_create(&draw_thread, NULL, (void *)draw_frame, NULL);
            /* Reset this */
            App->frameChanged = 0;
        }
    }
}


/* Draws/updates frame */
void draw_frame()
{
    /* The frame is drawing! */
    frame_still_drawing = 1;

    /* Draw frame and update view now */

    /* Get dhe frame, multithreaded if not caching */
    getMlvProcessedFrame16(App->videoMLV, App->currentFrameIndex, App->rawImage, (isMlvObjectCaching(App->videoMLV)) ? 1 : MAC_CORES);

    /* Update/refresh the view on main thread */
    [App->previewWindow performSelectorOnMainThread: @selector(updateView) withObject: nil waitUntilDone: YES];

    /* Reset, we don't want to stop */
    frame_still_drawing = 0;
}