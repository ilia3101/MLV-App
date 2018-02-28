/* Some properties of the app's design can be changed here */

/* DON'T JUDGE ME, I WROTE THIS BEFORE I HAD ANY TASTE IN CODE,
 * And fixing this would be too much effort */

#ifndef _app_layout_
#define _app_layout_

/* Basic stuff in here (don't try and deal with this file) */
#include "style_config.h"

/* ? keep as is or it looks bad */
#define ELEMENT_HEIGHT 32
/* Gap between elements and edge of window */
#define SIDE_GAP 15
#define TOP_GAP 30

/* Right sidebar stuff 
 * Made up of units with labels and sliders */

#define RIGHT_SIDEBAR_START 76 /* How far from the top it starts */
#define RIGHT_SIDEBAR_WIDTH (RIGHT_SIDEBAR_ELEMENT_WIDTH + SIDE_GAP_X_R * 2)
#define RIGHT_SIDEBAR_UNIT_HEIGHT 36
#define RIGHT_SIDEBAR_SLIDER_OFFSET 0
#define RIGHT_SIDEBAR_LABEL_OFFSET 12
/* Width of small label showing slider value */
#define RIGHT_SIDEBAR_VALUE_LABEL_WIDTH 50
#define RIGHT_SIDEBAR_LABEL_Y(UNIT_INDEX) (WINDOW_HEIGHT - (RIGHT_SIDEBAR_START + RIGHT_SIDEBAR_UNIT_HEIGHT * UNIT_INDEX) + RIGHT_SIDEBAR_LABEL_OFFSET)
#define RIGHT_SIDEBAR_SLIDER_Y(UNIT_INDEX) (WINDOW_HEIGHT - (RIGHT_SIDEBAR_START + RIGHT_SIDEBAR_UNIT_HEIGHT * UNIT_INDEX) + RIGHT_SIDEBAR_SLIDER_OFFSET)
#define RIGHT_SIDEBAR_X WINDOW_WIDTH - (RIGHT_SIDEBAR_ELEMENT_WIDTH + SIDE_GAP_X_R)
#define RIGHT_SIDEBAR_VALUE_LABEL_X WINDOW_WIDTH - (SIDE_GAP_X_R + RIGHT_SIDEBAR_VALUE_LABEL_WIDTH)
/* Offset of every block(gaps between some groups of sliders) */
#define BLOCK_OFFSET -28

/* End of sidebar stuff */


/* Left sidebar stuff */
#define LEFT_SIDEBAR_START 60 /* How far from the top it starts */
#define LEFT_SIDEBAR_WIDTH (LEFT_SIDEBAR_ELEMENT_WIDTH + SIDE_GAP_X_L * 2)
#define LEFT_SIDEBAR_UNIT_HEIGHT 43
#define LEFT_SIDEBAR_SLIDER_OFFSET 0
#define LEFT_SIDEBAR_LABEL_OFFSET 16
/* Width of small label showing slider value */
#define LEFT_SIDEBAR_VALUE_LABEL_WIDTH 25
#define LEFT_SIDEBAR_LABEL_Y(UNIT_INDEX) (WINDOW_HEIGHT - (LEFT_SIDEBAR_START + LEFT_SIDEBAR_UNIT_HEIGHT * UNIT_INDEX) + LEFT_SIDEBAR_LABEL_OFFSET)
#define LEFT_SIDEBAR_SLIDER_Y(UNIT_INDEX) (WINDOW_HEIGHT - (LEFT_SIDEBAR_START + LEFT_SIDEBAR_UNIT_HEIGHT * UNIT_INDEX) + LEFT_SIDEBAR_SLIDER_OFFSET)
#define LEFT_SIDEBAR_SLIDER_Y_BOTTOM(UNIT_INDEX) (SIDE_GAP + LEFT_SIDEBAR_UNIT_HEIGHT * UNIT_INDEX)
#define LEFT_SIDEBAR_X SIDE_GAP_X_L
#define LEFT_SIDEBAR_VALUE_LABEL_X WINDOW_WIDTH - (SIDE_GAP_X_L + LEFT_SIDEBAR_VALUE_LABEL_WIDTH)
/* End of sidebar stuff */


/* The preview window for the video */
#define PREVIEW_WINDOW_LEFT_MARGIN LEFT_SIDEBAR_WIDTH
#define PREVIEW_WINDOW_RIGHT_MARGIN RIGHT_SIDEBAR_WIDTH
#define PREVIEW_WINDOW_BOTTOM_MARGIN SIDE_GAP
#define PREVIEW_WINDOW_TOP_MARGIN TOP_GAP
#define PREVIEW_WINDOW_WIDTH WINDOW_WIDTH - PREVIEW_WINDOW_RIGHT_MARGIN - PREVIEW_WINDOW_LEFT_MARGIN
#define PREVIEW_WINDOW_HEIGHT WINDOW_HEIGHT - PREVIEW_WINDOW_TOP_MARGIN - PREVIEW_WINDOW_BOTTOM_MARGIN
#define TIMELINE_HEIGHT 32
/* End of preview window stuff */



/* Followinfg macros define rectangles for UI elements
 * Mostly used like this: initWithFrame: NSMakeRect( RIGHT_SIDEBAR_LABEL(unit_index) ) */

/* Name of control */
#define RIGHT_SIDEBAR_LABEL(unitIndex, height, verticalOffset) RIGHT_SIDEBAR_X, RIGHT_SIDEBAR_LABEL_Y(unitIndex) + verticalOffset, RIGHT_SIDEBAR_ELEMENT_WIDTH, height
/* To show value of control */
#define RIGHT_SIDEBAR_VALUE_LABEL(unitIndex, height, verticalOffset) RIGHT_SIDEBAR_VALUE_LABEL_X, RIGHT_SIDEBAR_LABEL_Y(unitIndex) + verticalOffset, RIGHT_SIDEBAR_VALUE_LABEL_WIDTH, height
/* For actual control(a slidre) */
#define RIGHT_SIDEBAR_SLIDER(unitIndex, height, verticalOffset) RIGHT_SIDEBAR_X, RIGHT_SIDEBAR_SLIDER_Y(unitIndex) + verticalOffset, RIGHT_SIDEBAR_ELEMENT_WIDTH, height

/* For an element in the leftsidebar(slider or button or text or anything) */
#define LEFT_SIDEBAR_ELEMENT_TOP(unitIndex, height, verticalOffset) LEFT_SIDEBAR_X, LEFT_SIDEBAR_SLIDER_Y(unitIndex) + verticalOffset, LEFT_SIDEBAR_ELEMENT_WIDTH, height
/* For an element at the bottom of the leftsidebar(slider or button or text or anything) */
#define LEFT_SIDEBAR_ELEMENT_BOTTOM(unitIndex, height, verticalOffset) LEFT_SIDEBAR_X, LEFT_SIDEBAR_SLIDER_Y_BOTTOM(unitIndex) + verticalOffset, LEFT_SIDEBAR_ELEMENT_WIDTH, height
/* Name of control */
#define LEFT_SIDEBAR_LABEL(unitIndex, height, verticalOffset) LEFT_SIDEBAR_X, LEFT_SIDEBAR_LABEL_Y(unitIndex) + verticalOffset, LEFT_SIDEBAR_ELEMENT_WIDTH, height

/* Where the preview window border is */
#define PREVIEW_WINDOW_BORDER_LOCATION PREVIEW_WINDOW_LEFT_MARGIN, PREVIEW_WINDOW_BOTTOM_MARGIN + TIMELINE_HEIGHT, PREVIEW_WINDOW_WIDTH, PREVIEW_WINDOW_HEIGHT - TIMELINE_HEIGHT
#define PREVIEW_MARGIN 6
/* Where the preview window itself is (inside the border with a margin of 6) */
#define PREVIEW_WINDOW_LOCATION PREVIEW_WINDOW_LEFT_MARGIN + PREVIEW_MARGIN, PREVIEW_WINDOW_BOTTOM_MARGIN + TIMELINE_HEIGHT + PREVIEW_MARGIN, PREVIEW_WINDOW_WIDTH - (PREVIEW_MARGIN*2), PREVIEW_WINDOW_HEIGHT - TIMELINE_HEIGHT - (PREVIEW_MARGIN*2)

/* Where the preview window (NSImageView) is */
#define TIMELINE_SLIDER_LOCATION PREVIEW_WINDOW_LEFT_MARGIN, SIDE_GAP, PREVIEW_WINDOW_WIDTH, TIMELINE_HEIGHT - 10



#endif