#ifndef AUDIO_MP3_H
#define AUDIO_MP3_H

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>

class AudioMP3 {
public:
    AudioMP3();
    void init();
    void play(const char* filepath);
    void pause();
    void loop();
    void stop();
    void deinit(); // Nouvelle méthode pour désinstaller proprement le driver
    bool isPlaying();

    // Ajout des méthodes pour récupérer le temps
    uint32_t getAudioCurrentTime();
    uint32_t getAudioFileDuration();

private:
    Audio audioI2S;
    bool playing;
};

#endif
