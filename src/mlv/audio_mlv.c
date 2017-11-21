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

/* Writes the MLV's audio in WAVE format to a given file path */
void writeMlvAudioToWave(mlvObject_t * video, char * path)
{
    if (!doesMlvHaveAudio(video)) return;

    uint64_t audio_size = getMlvAudioSize(video);
    uint64_t theoretic_size = getMlvAudioChannels(video) * getMlvSampleRate(video) * sizeof( uint16_t ) * getMlvFrames(video) / getMlvFramerate(video);
    uint64_t file_size = theoretic_size + sizeof(wave_header_t);

    /* Get audio */
    int16_t * audio_data = malloc( audio_size );
    getMlvAudioData(video, audio_data);

    wave_header_t wave_header = {
        .RIFF              = {'R','I','F','F'},
        .file_size         = file_size - 8,
        .WAVE              = {'W','A','V','E'},
        .bext_id           = {'b','e','x','t'},
        .bext_size         = sizeof( wave_bext_t ),
        .bext.time_reference = 0,//(uint64_t)(getMlvTmHour(video) * 3600 + getMlvTmMin(video) * 60 + getMlvTmSec(video)) * (uint64_t)getMlvSampleRate(video),
        .iXML_id           = {'i','X','M','L'},
        .iXML_size         = 1024,
        .fmt               = {'f','m','t',' '},
        .subchunk1_size    = 16,
        .audio_format      = 1,
        .num_channels      = getMlvAudioChannels(video),
        .sample_rate       = getMlvSampleRate(video),
        .byte_rate         = (getMlvSampleRate(video) * getMlvAudioChannels(video) * 16) / 8,
        .block_align       = (getMlvAudioChannels(video) * 16) / 8,
        .bits_per_sample   = 16,
        .data              = {'d','a','t','a'},
        .subchunk2_size    = theoretic_size
    };

    char temp[33];
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

    FILE * wave_file = fopen(path, "wb");

    /* Write header */
    fwrite(&wave_header, sizeof(wave_header_t), 1, wave_file);
    /* Write data */
    fwrite(audio_data, theoretic_size, 1, wave_file);

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
    if (!doesMlvHaveAudio(video)) return;

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
