#ifndef _processing2_h_
#define _processing2_h_

#include <stdint.h>

typedef struct Processing2 Processing_t;

/* Create */
Processing_t * new_Processing(int ResX, int ResY);
/* Delete */
void delete_Processing(Processing_t * processing);

/* Camera info, pass same matrix to both if only one matrix is known.
 * MatrixDaylight is ColorMatrix2 and MatrixTungsten is ColorMatrix1 */
void ProcessingSetCameraInfo( Processing_t * Processing,
                              double BlackLevel,
                              double WhiteLevel,
                              double * MatrixDaylight, 
                              double * MatrixTungsten );

/* Run processing */
void ProcessingDoProcessing32(float * Out); /* 0-1 output */
void ProcessingDoProcessing16(uint16_t * Out); /* 0-65535 */
void ProcessingDoProcessing8(uint8_t * Out); /* 0-255 */

/* Get resolution */
int ProcessingGetResX(Processing_t * processing);
int ProcessingGetResY(Processing_t * processing);

/* Set input image, demosaiced RGB, processing will make an internal copy */
void ProcessingSetInputImage(Processing_t * processing, float * Image);



/* Main setters/getters */
void ProcessingSetExposure(Processing_t * processing, double ExposureStops);
double ProcessingGetExposure(Processing_t * processing);

void ProcessingSetHighlightRolloff(Processing_t * processing, double Rolloff); /* -1 to +1, 0 default */
double ProcessingGetHighlightRolloff(Processing_t * processing);

void ProcessingSetWBKelvin(Processing_t * Processing, double Kelvin);
double ProcessingGetWBKelvin(Processing_t * Processing);

void ProcessingSetWBTint(Processing_t * Processing, double Tint);
double ProcessingGetWBTint(Processing_t * Processing);

void ProcessingSetWBFromImage(Processing_t * Processing, int X, int Y, int Radius);

void ProcessingSetSaturation(Processing_t * Processing, double Saturation); /* -1 to +1, 0 default */
double ProcessingGetSaturation(Processing_t * Processing);


#endif
