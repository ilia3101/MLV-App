#ifndef _audio_mlv_
#define _audio_mlv_

/* mlvObject_t */
#include "mlv_object.h"

/* Usefull macros */
#include "macros.h"

/* Writes cut MLV audio into Broacast Wave format */
void writeMlvAudioToWaveCut(mlvObject_t * video, char * path, uint32_t cut_in, uint32_t cut_out);
/* Writes MLV audio into Broacast Wave format */
void writeMlvAudioToWave(mlvObject_t * video, char * path);
/* Returnes pointer to the MLV audio buffer and audio size as parameter */
void * getMlvAudioData(mlvObject_t * video, uint64_t * output_audio_size);

#endif
