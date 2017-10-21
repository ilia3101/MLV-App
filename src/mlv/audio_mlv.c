/* http://soundfile.sapp.org/doc/WaveFormat/ */
#pragma pack(push,1)
typedef struct {
    /* The "RIFF" chunk descriptor */
    uint8_t chunk_id[4];
    int32_t chunk_size;
    uint8_t format[4];
    /* The "fmt" sub-chunk */
    uint8_t sub_chunk_id[4];
    int32_t sub_chunk_1_size;
    int16_t audio_format; /* 1 = PCM */
    int16_t num_channels;
    int32_t sample_rate;
    int32_t byte_rate;
    int16_t block_align;
    int16_t bits_per_sample;
    /* The "data" sub-chunk */
    uint8_t sub_chunk_2_id[4];
    int32_t sub_chunk_2_size;
} wave_header_t;
#pragma pack(pop)

/* Writes the MLV's audio in WAVE format to a given file path */
void writeMlvAudioToWave(mlvObject_t * video, char * path)
{
    uint64_t audio_size = getMlvAudioSize(video);
    uint64_t file_size = audio_size + sizeof(wave_header_t);

    /* Get audio */
    int16_t * audio_data = malloc( audio_size );
    getMlvAudioData(video, audio_data);

    wave_header_t wave_header = {
        .chunk_id          =  {'R','I','F','F'},
        .chunk_size        =  file_size - 8,
        .format            =  {'W','A','V','E'},
        .sub_chunk_id      =  {'f','m','t',' '},
        .sub_chunk_1_size  =  16,
        .audio_format      =  1,
        .num_channels      =  getMlvAudioChannels(video),
        .sample_rate       =  getMlvSampleRate(video),
        .byte_rate         =  (getMlvSampleRate(video) * getMlvAudioChannels(video) * 16) / 8,
        .block_align       =  (getMlvAudioChannels(video) * 16) / 8,
        .bits_per_sample   =  16,
        .sub_chunk_2_id    =  {'d','a','t','a'},
        .sub_chunk_2_size  =  audio_size
    };

    FILE * wave_file = fopen(path, "wb");

    /* Write header */
    fwrite(&wave_header, sizeof(wave_header_t), 1, wave_file);
    /* Write data */
    fwrite(audio_data, audio_size, 1, wave_file);

    fclose(wave_file);
    free(audio_data);
}

/* When allocating memory for audio use this */
uint64_t getMlvAudioSize(mlvObject_t * video)
{
    uint64_t size = 0;
    for (uint32_t i = 0; i < video->audios; ++i)
    {
        size += video->audio_sizes[i];
    }
    return size;
}

void getMlvAudioData(mlvObject_t * video, int16_t * outputAudio)
{
    /* Keep track of bytes of audio */
    uint64_t audio_size = 0;

    /* uint8_t pointer to audio data to work with bytes */
    uint8_t * output_audio = (uint8_t *)outputAudio;

    for (uint32_t i = 0; i < video->audios; ++i)
    {
        pthread_mutex_lock(&video->main_file_mutex);
        /* Go to audio block position */
        file_set_pos(video->file, video->audio_offsets[i], SEEK_SET);

        /* Read to location of audio */
        fread(output_audio + audio_size, video->audio_sizes[i], 1, video->file);
        pthread_mutex_unlock(&video->main_file_mutex);

        /* New audio position */
        audio_size += video->audio_sizes[i];
    }
}
