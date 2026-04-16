#ifndef AUDIO_H
#define AUDIO_H

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AUDIO_STATE_IDLE,
    AUDIO_STATE_RECORDING,
    AUDIO_STATE_PLAYING,
} audio_state_t;

// Initialize I2S and the ES8311 codec. Allocates recording buffer in PSRAM.
esp_err_t audio_init(void);

// Start recording microphone input into the internal buffer.
// Records until audio_stop_recording() is called or AUDIO_MAX_RECORD_SEC is reached.
esp_err_t audio_start_recording(void);

// Stop recording. Returns the number of bytes recorded.
size_t audio_stop_recording(void);

// Play back the contents of the internal recording buffer through the speaker.
esp_err_t audio_start_playback(void);

// Stop playback.
void audio_stop_playback(void);

// Get current state.
audio_state_t audio_get_state(void);

// Get the recording buffer and its length (for future: upload to server).
const uint8_t *audio_get_buffer(size_t *out_len);

#endif // AUDIO_H
