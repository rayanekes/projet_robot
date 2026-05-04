#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include <Arduino.h>
#include <driver/i2s_std.h> // API nouvelle — utilisée pour le micro et le haut-parleur

// --- Configuration du Microphone INMP441 (I2S_NUM_0 / Entrée) ---
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_MIC_SCK 4 // GPIO Matrix : côté gauche ESP32 (Breadboard 1)
#define I2S_MIC_WS 5
#define I2S_MIC_SD 6

// --- Fréquences d'échantillonnage ---
#define SAMPLE_RATE_MIC 16000 // STT Whisper
#define SAMPLE_RATE_TTS 24000 // TTS Kokoro (sortie I2S locale)

// --- Configuration du Haut-Parleur MAX98357A (I2S_NUM_1 / Sortie) ---
// GPIOs libres, aucun conflit avec Octal SPI, écran, micro ou IR.
#define I2S_SPK_BCLK 17 // Bit Clock
#define I2S_SPK_WS 18   // Word Select (LRC)
#define I2S_SPK_DIN 21  // Data In (DOUT du point de vue S3)

// Compensation de volume pour baffle 8Ω :
// Le MAX98357A est optimisé pour 4Ω (3.2W max).
// Sur 8Ω il ne délivre que ~1.4W avant saturation.
// On limite à 60% de l'amplitude pour éviter le clipping/distorsion.
#define SPK_VOLUME_SCALE_8OHM                                                  \
  0.60f // Passer à 1.0f si tu remplaces par un baffle 4Ω

class Audio_I2S {
public:
  Audio_I2S(); // Constructeur pour initialiser le volume
  void initMic();
  void uninstallMic();
  size_t readMic(int16_t *buffer, size_t bufferSize);

  void initSpeaker();
  void uninstallSpeaker();
  // Joue le buffer PCM int16 mono 24kHz.
  // Applique le facteur de volume actuel.
  void writeSpeaker(const uint8_t *buffer, size_t bufferSize);
  // Définit le volume en pourcentage (0-100)
  void setVolume(int percentage);

private:
  int32_t *_raw_buf = nullptr;             // Buffer DMA mic INMP441
  i2s_chan_handle_t _mic_handle = nullptr; // Handle mic
  i2s_chan_handle_t _spk_handle = nullptr; // Handle speaker
  float _current_volume_scale;             // Facteur de volume dynamique
};

#endif // AUDIO_I2S_H
