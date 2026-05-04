#include "audio_i2s.h"

void Audio_I2S::initMic() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // Requis pour éviter de lire les anciens buffers (important pour le VAD/STT)
  chan_cfg.auto_clear = true; 

  esp_err_t err = i2s_new_channel(&chan_cfg, &_mic_handle, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S-MIC] Erreur driver_install: %s\n", esp_err_to_name(err));
    return;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_MIC),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_MIC_SCK,
          .ws   = (gpio_num_t)I2S_MIC_WS,
          .dout = I2S_GPIO_UNUSED,
          .din  = (gpio_num_t)I2S_MIC_SD,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv   = false,
          },
      },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // INMP441 L/R pin to GND = Left channel

  err = i2s_channel_init_std_mode(_mic_handle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[I2S-MIC] Erreur set_pin: %s\n", esp_err_to_name(err));
    return;
  }

  i2s_channel_enable(_mic_handle);

  // Buffer DMA intermédiaire de lecture 32-bits (SRAM interne uniquement)
  _raw_buf = (int32_t *)heap_caps_malloc(512 * sizeof(int32_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!_raw_buf) {
    Serial.println("[I2S-MIC] ERREUR: malloc DMA buffer échoué !");
  }
  Serial.println("[I2S-MIC] Microphone INMP441 initialisé (API Nouvelle).");
}

// ── Haut-parleur MAX98357A (I2S_NUM_1) — Nouvelle API i2s_std.h ────────────
// Le microphone reste sur l'API legacy (I2S_NUM_0), les deux coexistent sans
// conflit.

void Audio_I2S::initSpeaker() {
  // Allocation du canal TX sur I2S_NUM_1
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  chan_cfg.auto_clear =
      true; // Vider le DMA avant lecture = pas de pop au démarrage

  esp_err_t err = i2s_new_channel(&chan_cfg, &_spk_handle, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S-SPK] Erreur i2s_new_channel: %s\n",
                  esp_err_to_name(err));
    return;
  }

  // Configuration standard : 24kHz, 16-bit, Mono
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_TTS),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)I2S_SPK_BCLK,
              .ws = (gpio_num_t)I2S_SPK_WS,
              .dout = (gpio_num_t)I2S_SPK_DIN,
              .din = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  err = i2s_channel_init_std_mode(_spk_handle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[I2S-SPK] Erreur init_std_mode: %s\n", esp_err_to_name(err));
    return;
  }

  err = i2s_channel_enable(_spk_handle);
  Serial.printf("[I2S-SPK] MAX98357A prêt (SR=%d Hz, BCLK=%d, WS=%d, DIN=%d, "
                "Vol=%.0f%%) : %s\n",
                SAMPLE_RATE_TTS, I2S_SPK_BCLK, I2S_SPK_WS, I2S_SPK_DIN,
                SPK_VOLUME_SCALE_8OHM * 100.0f, esp_err_to_name(err));
}

void Audio_I2S::uninstallSpeaker() {
  if (_spk_handle) {
    i2s_channel_disable(_spk_handle);
    i2s_del_channel(_spk_handle);
    _spk_handle = nullptr;
  }
}

void Audio_I2S::writeSpeaker(const uint8_t *buffer, size_t bufferSize) {
  if (!_spk_handle || !buffer || bufferSize == 0)
    return;

  // Scaling amplitude pour baffle 8Ω (évite le clipping/distorsion)
  // Buffer statique en DRAM pour éviter la fragmentation heap sur le Core 1
  static DRAM_ATTR int16_t scaled[512];

  const int16_t *src = (const int16_t *)buffer;
  const size_t nTotal = bufferSize / 2; // Nombre de samples int16
  size_t written = 0;

  for (size_t offset = 0; offset < nTotal; offset += 512) {
    size_t chunk = (nTotal - offset < 512) ? (nTotal - offset) : 512;

    for (size_t i = 0; i < chunk; i++) {
      int32_t s = (int32_t)(src[offset + i] * SPK_VOLUME_SCALE_8OHM);
      // Clip pour éviter l'overflow int16
      scaled[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }

    i2s_channel_write(_spk_handle, scaled, chunk * 2, &written,
                      pdMS_TO_TICKS(200));
  }
}

void Audio_I2S::uninstallMic() { 
  if (_mic_handle) {
    i2s_channel_disable(_mic_handle);
    i2s_del_channel(_mic_handle);
    _mic_handle = nullptr;
  }
}

size_t Audio_I2S::readMic(int16_t *buffer, size_t bufferSize) {
  size_t bytesRead = 0;
  if (!_raw_buf || !_mic_handle)
    return 0;

  // Timeout de 100ms au lieu de portMAX_DELAY pour éviter de "freezer" la carte si le micro est mal branché
  esp_err_t err = i2s_channel_read(_mic_handle, _raw_buf, 512 * 4, &bytesRead, pdMS_TO_TICKS(100));
  if (err != ESP_OK) return 0;

  int samplesRead = bytesRead / 4;
  for (int i = 0; i < samplesRead; i++) {
    // INMP441 : audio 24-bit MSB-aligned dans un mot 32-bit.
    // Shift >> 14 pour obtenir un 16-bit propre avec une bonne sensibilité.
    int32_t val = _raw_buf[i] >> 14;
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    buffer[i] = (int16_t)val;
  }
  return samplesRead * 2;
}
