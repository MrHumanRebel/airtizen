/*
 * AirTizen Tizen audio adapter.
 * Intended target: Samsung Tizen native/wasm integration.
 * Final implementation writes PCM frames received from AirPlayServerLib to Tizen Audio I/O.
 */
#include <stddef.h>
int airtizen_audio_open(int sample_rate, int channels, int bits_per_sample) {
    (void)sample_rate; (void)channels; (void)bits_per_sample;
    return 0;
}
int airtizen_audio_write(const void *pcm, size_t bytes) {
    (void)pcm;
    return (int)bytes;
}
void airtizen_audio_close(void) {}
