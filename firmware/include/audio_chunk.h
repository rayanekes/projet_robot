#ifndef AUDIO_CHUNK_H
#define AUDIO_CHUNK_H

#include <Arduino.h>

struct AudioChunk {
    uint8_t* data;
    size_t length;
};

#endif
