/* Background threads, including fps timer and frame renderer */

#ifndef _background_thread_h_
#define _background_thread_h_

/* Starts frame drawer on a background thread, run once at start of app */
void beginFrameDrawing();


/* DON'T TOUCH THESE OR YOU'LL DIE */
void framerate_timer();
void draw_frame();

#endif