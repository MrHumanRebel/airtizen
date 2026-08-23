#include <stddef.h>

#ifdef __TIZEN__
#include <audio_io.h>

void airtizen_audio_close(void);

static audio_out_h g_output;
static int g_rate;
static int g_channels;
static int g_bits;

int airtizen_audio_open(int sample_rate, int channels, int bits_per_sample) {
    int rc;
    audio_channel_e channel;
    if (g_output && sample_rate == g_rate && channels == g_channels && bits_per_sample == g_bits) return 0;
    airtizen_audio_close();
    if (channels != 1 && channels != 2) return -1;
    if (bits_per_sample != 16) return -2;
    channel = channels == 2 ? AUDIO_CHANNEL_STEREO : AUDIO_CHANNEL_MONO;
    rc = audio_out_create_new(sample_rate, channel, AUDIO_SAMPLE_TYPE_S16_LE,
                              SOUND_TYPE_MEDIA, &g_output);
    if (rc != AUDIO_IO_ERROR_NONE) { g_output = NULL; return rc; }
    rc = audio_out_prepare(g_output);
    if (rc != AUDIO_IO_ERROR_NONE) { audio_out_destroy(g_output); g_output = NULL; return rc; }
    g_rate = sample_rate; g_channels = channels; g_bits = bits_per_sample;
    return 0;
}

int airtizen_audio_write(const void *pcm, size_t bytes) {
    if (!g_output || !pcm) return -1;
    return audio_out_write(g_output, pcm, (unsigned int)bytes);
}

void airtizen_audio_close(void) {
    if (!g_output) return;
    audio_out_unprepare(g_output);
    audio_out_destroy(g_output);
    g_output = NULL;
}
#else
/* Host build validates protocol/runtime. Audio device exists only on Tizen. */
static int g_open;
int airtizen_audio_open(int sample_rate, int channels, int bits_per_sample) {
    if (sample_rate <= 0 || (channels != 1 && channels != 2) || bits_per_sample != 16) return -1;
    g_open = 1;
    return 0;
}
int airtizen_audio_write(const void *pcm, size_t bytes) {
    return (g_open && pcm) ? (int)bytes : -1;
}
void airtizen_audio_close(void) { g_open = 0; }
#endif
