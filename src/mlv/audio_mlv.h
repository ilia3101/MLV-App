#ifndef _audio_mlv_
#define _audio_mlv_

/* mlvObject_t */
#include "mlv_object.h"

/* Usefull macros */
#include "macros.h"

/* Writes the MLV's audio in WAVE format to a given file path */
void writeMlvAudioToWave(mlvObject_t * video, char * path);
void writeMlvAudioToWaveCut(mlvObject_t * video, char * path, uint32_t cut_in, uint32_t cut_out);
/* When allocating memory for audio use this */
uint64_t getMlvAudioSize(mlvObject_t * video);
/* Gets all audio data 4 u */
void getMlvAudioData(mlvObject_t * video, int16_t * outputAudio);

#endif
