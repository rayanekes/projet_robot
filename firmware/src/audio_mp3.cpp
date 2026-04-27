#include "audio_mp3.h"

// Définitions des broches I2S pour le MAX98357A
#define I2S_DOUT 12
#define I2S_BCLK 27
#define I2S_LRC 26

// Explicitly initialize Audio on I2S_NUM_1 to avoid conflict with the microphone on I2S_NUM_0
// The first parameter is internalDAC (false to use external I2S DAC like MAX98357A)
// The second is channelEnabled (3 for both channels), the third is i2sPort (I2S_NUM_1)
AudioMP3::AudioMP3() : audioI2S(false, 3, I2S_NUM_1), playing(false) {}

void AudioMP3::init() {
    audioI2S.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audioI2S.setVolume(21); // Volume de 0 à 21 (Max)
}

void AudioMP3::play(const char* filepath) {
    audioI2S.connecttoFS(SD, filepath);
    playing = true;
    Serial.print("Lecture MP3 : ");
    Serial.println(filepath);
}

void AudioMP3::pause() {
    audioI2S.pauseResume();
    playing = !playing; // bascule d'état (simplifié)
}

void AudioMP3::stop() {
    audioI2S.stopSong();
    playing = false;
}

void AudioMP3::deinit() {
    audioI2S.stopSong();
    playing = false;
    // Désinstallation forcée du driver I2S_NUM_1 utilisé par la librairie
    // pour permettre au haut-parleur IA de reprendre la main.
    i2s_driver_uninstall(I2S_NUM_1);
}

void AudioMP3::loop() {
    audioI2S.loop();
}

bool AudioMP3::isPlaying() {
    return audioI2S.isRunning();
}

uint32_t AudioMP3::getAudioCurrentTime() {
    return audioI2S.getAudioCurrentTime();
}

uint32_t AudioMP3::getAudioFileDuration() {
    return audioI2S.getAudioFileDuration();
}
