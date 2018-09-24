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
/* Fills mlvObject_t fields, allocates audio buffer and sets audio size */
void readMlvAudioData(mlvObject_t * video);

#endif
