#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include <Arduino.h>
#include <driver/i2s.h>

// --- Configuration du Microphone INMP441 (I2S_NUM_0 / Entrée) ---
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_MIC_WS   25
#define I2S_MIC_SCK  32
#define I2S_MIC_SD   33

// --- Configuration du Haut-Parleur MAX98357A (I2S_NUM_1 / Sortie) ---
#define I2S_SPK_PORT I2S_NUM_1
#define I2S_SPK_WS   26
#define I2S_SPK_BCLK 27
#define I2S_SPK_DIN  22

// Fréquence d'échantillonnage
#define SAMPLE_RATE_MIC 16000 // STT Whisper
#define SAMPLE_RATE_SPK 22050 // TTS Piper

class Audio_I2S {
public:
    void initMic();
    void initSpeaker();
    void uninstallMic();
    void uninstallSpeaker();

    // Fonction pour lire des données depuis le microphone (bloquante ou avec délai)
    size_t readMic(int16_t *buffer, size_t bufferSize);

    // Fonction pour écrire des données vers le haut-parleur
    void writeSpeaker(const uint8_t *buffer, size_t bufferSize);

private:
    int32_t* _raw_buf = nullptr; // Buffer DMA pour INMP441 (FIX-007)
};

#endif // AUDIO_I2S_H
