#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <inttypes.h>

#include "audio_mlv.h"
#include "video_mlv.h"

#include "mcraw/mcraw.h"

/* Usefull macros */
#include "macros.h"

#define MIN(a,b) (((a)<(b))?(a):(b))

static const char * iXML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<BWFXML>"
    "<IXML_VERSION>1.5</IXML_VERSION>"
    "<PROJECT>%s</PROJECT>"
    "<NOTE>%s</NOTE>"
    "<CIRCLED>FALSE</CIRCLED>"
    "<BLACKMAGIC-KEYWORDS>%s</BLACKMAGIC-KEYWORDS>"
    "<TAPE>%d</TAPE>"
    "<SCENE>%d</SCENE>"
    "<BLACKMAGIC-SHOT>%d</BLACKMAGIC-SHOT>"
    "<TAKE>%d</TAKE>"
    "<BLACKMAGIC-ANGLE>ms</BLACKMAGIC-ANGLE>"
    "<SPEED>"
    "<MASTER_SPEED>%d/%d</MASTER_SPEED>"
    "<CURRENT_SPEED>%d/%d</CURRENT_SPEED>"
    "<TIMECODE_RATE>%d/%d</TIMECODE_RATE>"
    "<TIMECODE_FLAG>NDF</TIMECODE_FLAG>"
    "</SPEED>"
    "</BWFXML>";

#pragma pack(push,1)

typedef struct {
    char description[256];
    char originator[32];
    char originator_reference[32];
    char origination_date[10];      //yyyy:mm:dd
    char origination_time[8];       //hh:mm:ss
    uint64_t time_reference;
    uint16_t version;
    uint8_t umid[64];
    int16_t loudness_value;
    int16_t loudness_range;
    int16_t max_true_peak_level;
    int16_t max_momentary_loudness;
    int16_t max_short_term_loudness;
    uint8_t reserved[180];
    char coding_history[4];
} wave_bext_t;

typedef struct {
    //file header
    char RIFF[4];               // "RIFF"
    uint32_t file_size;
    char WAVE[4];               // "WAVE"
    //bext subchunk
    char bext_id[4];
    uint32_t bext_size;
    wave_bext_t bext;
    //iXML subchunk
    char iXML_id[4];
    uint32_t iXML_size;
    char iXML[1024];
    //subchunk1
    char fmt[4];                // "fmt"
    uint32_t subchunk1_size;    // 16
    uint16_t audio_format;      // 1 (PCM)
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    //subchunk2
    char data[4];               // "data"
    uint32_t subchunk2_size;
    //audio data start
} wave_header_t;

#pragma pack(pop)

static uint64_t file_set_pos(FILE *stream, uint64_t offset, int whence)
{
#if defined(__WIN32)
    return fseeko64(stream, offset, whence);
#else
    return fseek(stream, offset, whence);
#endif
}

/* When allocating memory for audio use this */
static uint64_t initMlvAudioSize(mlvObject_t * video)
{
    uint64_t size = 0;
    for (uint32_t i = 0; i < video->audios; ++i)
    {
        size += video->audio_index[i].frame_size;
    }
    return size;
}

/* Generate the header for the audio wave file */
static wave_header_t generateMlvAudioToWaveHeader(mlvObject_t * video, uint64_t wave_data_size, uint32_t frame_offset)
{
    uint64_t file_size = wave_data_size + sizeof(wave_header_t);
    /* time reference is the audio sample count from recording start */
    uint64_t time_reference = (uint64_t)( (double)( (video->video_index[0].frame_number + frame_offset) * getMlvSampleRate(video) ) / (double)getMlvFramerateOrig(video) );

    wave_header_t wave_header = {
        .RIFF                = {'R','I','F','F'},
        .file_size           = file_size - 8,
        .WAVE                = {'W','A','V','E'},
        .bext_id             = {'b','e','x','t'},
        .bext_size           = sizeof( wave_bext_t ),
        .bext.time_reference = time_reference,
        .bext.version        = 0x1,
        .bext.coding_history = {'P','C','M',' '},
        .iXML_id             = {'i','X','M','L'},
        .iXML_size           = 1024,
        .fmt                 = {'f','m','t',' '},
        .subchunk1_size      = 16,
        .audio_format        = 1,
        .num_channels        = getMlvAudioChannels(video),
        .sample_rate         = getMlvSampleRate(video),
        .byte_rate           = (getMlvSampleRate(video) * getMlvAudioChannels(video) * getMlvAudioBitsPerSample(video)) / 8,
        .block_align         = (getMlvAudioChannels(video) * getMlvAudioBitsPerSample(video)) / 8,
        .bits_per_sample     = 16,
        .data                = {'d','a','t','a'},
        .subchunk2_size      = wave_data_size
    };

    char temp[33] = { 0 };
    snprintf(temp, sizeof(temp), "Exported MLV Audio");
    memcpy(wave_header.bext.description, temp, 32);
    snprintf(temp, sizeof(temp), "%s", getMlvCamera(video));
    memcpy(wave_header.bext.originator, temp, 32);
    snprintf(temp, sizeof(temp), "JPCAN%04d%.8s%02d%02d%02d%09d", getMlvCameraModel(video), getMlvCameraSerial(video), getMlvTmHour(video), getMlvTmMin(video), getMlvTmSec(video), rand());
    memcpy(wave_header.bext.originator_reference, temp, 32);
    snprintf(temp, sizeof(temp), "%04d:%02d:%02d", getMlvTmYear(video), getMlvTmMonth(video), getMlvTmDay(video));
    memcpy(wave_header.bext.origination_date, temp, 10);
    snprintf(temp, sizeof(temp), "%02d:%02d:%02d", getMlvTmHour(video), getMlvTmMin(video), getMlvTmSec(video));
    memcpy(wave_header.bext.origination_time, temp, 8);

    char * project = "MLV App";
    char * notes = "";
    char * keywords = "";
    int tape = 1, scene = 1, shot = 1, take = 1;
    int fps_denom = video->MLVI.sourceFpsDenom;
    int fps_nom = video->MLVI.sourceFpsNom;
    snprintf(wave_header.iXML, wave_header.iXML_size, iXML, project, notes, keywords, tape, scene, shot, take, fps_nom, fps_denom, fps_nom, fps_denom, fps_nom, fps_denom);

    return wave_header;
}

/* Writes the MLV's audio in WAVE format to a given file path, between the frames cut_in & cut_out (1<=..<=getMlvFrames) */
void writeMlvAudioToWaveCut(mlvObject_t * video, char * path, uint32_t cut_in, uint32_t cut_out)
{
    if (!doesMlvHaveAudio(video)) return;
    if( cut_in < 1 || cut_out > getMlvFrames(video) ) return;

    int32_t frames = cut_out - ( cut_in - 1 );
    if( frames <= 0 ) return;

    /* Calculate the sum of audio sample sizes for all audio channels */
    uint64_t audio_sample_size = getMlvAudioChannels(video) * (getMlvAudioBitsPerSample(video) / 8);
    /* Calculate the audio alignement block size in bytes */
    uint16_t block_align = audio_sample_size * 1024;
    /* Calculate cut_in offset */
    uint64_t in_offset = (uint64_t)( (double)(getMlvSampleRate(video) * audio_sample_size * ( cut_in - 1 )) / (double)getMlvFramerateOrig(video) );
    /* Make sure in offset value is multiple of sum of all channel sample sizes */
    uint64_t in_offset_aligned = in_offset - (in_offset % audio_sample_size);
    /* Calculate cut audio size */
    uint64_t cut_audio_size = (uint64_t)( (double)(getMlvSampleRate(video) * audio_sample_size * frames) / (double)getMlvFramerate(video) );
    /* Check if output_audio_size is multiple of 4096 bytes and add one more block */
    uint64_t cut_audio_size_aligned = cut_audio_size - (cut_audio_size % block_align) + block_align;
    /* Wav audio data size */
    uint64_t wave_data_size = MIN(cut_audio_size_aligned, video->audio_size);
    /* Get wav header */
    wave_header_t wave_header = generateMlvAudioToWaveHeader(video, wave_data_size, cut_in - 1);

    FILE * wave_file = fopen(path, "wb");
    if( !wave_file ) return;
    /* Write header */
    fwrite(&wave_header, sizeof(wave_header_t), 1, wave_file);
    /* Write data, shift buffer by in_offset_aligned */
    fwrite(video->audio_data + in_offset_aligned, wave_data_size, 1, wave_file);

    fclose(wave_file);
}

/* Writes the MLV's audio in WAVE format to a given file path */
void writeMlvAudioToWave(mlvObject_t * video, char * path)
{
    if (!doesMlvHaveAudio(video)) return;

    /* Get wav header */
    wave_header_t wave_header = generateMlvAudioToWaveHeader(video, video->audio_size, 0);

    FILE * wave_file = fopen(path, "wb");
    if( !wave_file ) return;
    /* Write header */
    fwrite(&wave_header, sizeof(wave_header_t), 1, wave_file);
    /* Write data */
    fwrite(video->audio_data, video->audio_size, 1, wave_file);

    fclose(wave_file);
}

void readMlvAudioData(mlvObject_t * video)
{
    if (!doesMlvHaveAudio(video)) return;

    int fread_err = 1;
    uint64_t mlv_audio_buffer_offset = 0;
    uint64_t allocated_size = initMlvAudioSize(video);
    uint8_t * mlv_audio_buffer = allocated_size ? malloc(allocated_size) : NULL;
    uint64_t mlv_audio_size = allocated_size;

    if (mlv_audio_size && !mlv_audio_buffer)
    {
#ifndef STDOUT_SILENT
    printf("Audio frame buffer allocation error");
#endif
        return;
    }

    if (isMcrawLoaded(video))
    {
        mr_item_t hdr_item = {};

        mlv_audio_size = 0;

        for (uint32_t i = 0; i < video->audios; ++i)
        {
            pthread_mutex_lock(video->main_file_mutex + video->audio_index[i].chunk_num);

            /* Go to audio block position */
            file_set_pos(video->file[0], video->audio_index[i].block_offset, SEEK_SET);

            /* Read data header */
            fread_err &= fread(&hdr_item, sizeof(mr_item_t), 1, video->file[0]);

            if (mlv_audio_size + hdr_item.size > allocated_size)
            {
                allocated_size = mlv_audio_size + hdr_item.size + (video->audios - i) * hdr_item.size;
                mlv_audio_buffer = realloc(mlv_audio_buffer, allocated_size);
            }

            /* Read to location of audio */
            fread_err &= fread(mlv_audio_buffer + mlv_audio_size, hdr_item.size, 1, video->file[0]);

            if (i == 0)
            {
                mr_audio_metadata_t metadata = {};
                if (fread(&metadata, sizeof(mr_audio_metadata_t), 1, video->file[0]) == 1)
                {
                    if (metadata.item.type == AUDIO_DATA_METADATA) {
                        video->audio_index[i].frame_time = metadata.timestampNs / 1000000;
                    }
                }
            }

            pthread_mutex_unlock(video->main_file_mutex + video->audio_index[i].chunk_num);

            /* New audio position */
            mlv_audio_size += hdr_item.size;
        }

        if (video->audio_index[0].frame_time == 0) {
            video->audio_index[0].frame_time = video->video_index[0].frame_time;
        }
    }
    else
    {
        for (uint32_t i = 0; i < video->audios; ++i)
        {
            pthread_mutex_lock(video->main_file_mutex + video->audio_index[i].chunk_num);
            /* Go to audio block position */
            file_set_pos(video->file[video->audio_index[i].chunk_num], video->audio_index[i].frame_offset, SEEK_SET);
            /* Read to location of audio */
            fread_err &= fread(mlv_audio_buffer + mlv_audio_buffer_offset, video->audio_index[i].frame_size, 1, video->file[video->audio_index[i].chunk_num]);
            pthread_mutex_unlock(video->main_file_mutex + video->audio_index[i].chunk_num);
            /* New audio position */
            mlv_audio_buffer_offset += video->audio_index[i].frame_size;
        }
    }

    if(!fread_err)
    {
#ifndef STDOUT_SILENT
        printf("Audio frame data read error");
#endif
        free(mlv_audio_buffer);
        return;
    }

    /* Calculate the sum of audio sample sizes for all audio channels */
    uint64_t audio_sample_size = getMlvAudioChannels(video) * (getMlvAudioBitsPerSample(video) / 8);
    /* Calculate the audio alignement block size in bytes */
    uint16_t block_align = audio_sample_size * 1024;

    /* Get time difference of first video and audio frames and calculate the sync offset */
    uint64_t negative_offset = 0;
    uint64_t positive_offset = 0;
    int64_t sync_offset = (int64_t)( ( (double)video->video_index[0].frame_time - (double)video->audio_index[0].frame_time ) * (double)( getMlvSampleRate(video) * audio_sample_size / 1000000.0 ) );
    if(sync_offset >= 0) negative_offset = (uint64_t)sync_offset - ((uint64_t)sync_offset % audio_sample_size); // Make sure value is multiple of sum of all channel sample sizes
    else positive_offset = (uint64_t)(-sync_offset) - ((uint64_t)(-sync_offset) % audio_sample_size);

    /* Calculate synced audio size */
    uint64_t synced_audio_size = mlv_audio_size - negative_offset + positive_offset;
    /* Check if synced_audio_size is multiple of 'block_align' bytes and add one more block */
    uint64_t synced_audio_size_aligned = synced_audio_size - (synced_audio_size % block_align) + block_align;

    /* Allocate synced audio buffer */
    video->audio_data = calloc( synced_audio_size_aligned, 1 );
    if(!video->audio_data)
    {
#ifndef STDOUT_SILENT
        printf("Synced and aligned audio buffer allocation error");
#endif
        free(mlv_audio_buffer);
        return;
    }

    /* Copy cut/shifted audio data to the synced audio buffer */
    memcpy(video->audio_data + positive_offset, mlv_audio_buffer + negative_offset, mlv_audio_size - negative_offset);
    free(mlv_audio_buffer);

    /* Calculate theoretical audio size according to fps */
    uint64_t theoretic_size = (uint64_t)( (double)( getMlvSampleRate(video) * audio_sample_size * getMlvFrames(video) ) / getMlvFramerateOrig(video) );
    /* Check if theoretic_size is multiple of 'block_align' bytes and add one more block */
    uint64_t theoretic_size_aligned = theoretic_size - (theoretic_size % block_align) + block_align;
    /* Check calculated synced_audio_size_aligned against theoretic_size_aligned */
    uint64_t final_audio_size_aligned = MIN(theoretic_size_aligned, synced_audio_size_aligned);

    video->audio_buffer_size = synced_audio_size_aligned;
    video->audio_size = final_audio_size_aligned;

#ifndef STDOUT_SILENT
    printf("\nnegative_offset = %lu, positive_offset = %lu, synced_audio_size_aligned = %lu, final_audio_size_aligned = %lu\n", negative_offset, positive_offset, synced_audio_size_aligned, final_audio_size_aligned);
#endif
}
