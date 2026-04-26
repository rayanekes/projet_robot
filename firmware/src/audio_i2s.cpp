#include "audio_i2s.h"

void Audio_I2S::initMic() {
    i2s_config_t i2s_mic_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE_MIC,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false
    };

    i2s_pin_config_t i2s_mic_pins = {
        .bck_io_num = I2S_MIC_SCK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_SD
    };

    i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &i2s_mic_pins);
    
    _raw_buf = (int32_t*)heap_caps_malloc(1024 * sizeof(int32_t), MALLOC_CAP_DMA);
    Serial.println("Microphone I2S (INMP441) initialisé.");
}

void Audio_I2S::initSpeaker() {
    // Couper physiquement le bruit avant d'allumer le driver
    pinMode(I2S_SPK_BCLK, INPUT);
    pinMode(I2S_SPK_WS, INPUT);
    pinMode(I2S_SPK_DIN, INPUT);
    delay(50);

    i2s_config_t i2s_spk_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE_SPK,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t i2s_spk_pins = {
        .bck_io_num = I2S_SPK_BCLK,
        .ws_io_num = I2S_SPK_WS,
        .data_out_num = I2S_SPK_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_SPK_PORT, &i2s_spk_config, 0, NULL);
    i2s_set_pin(I2S_SPK_PORT, &i2s_spk_pins);
    i2s_zero_dma_buffer(I2S_SPK_PORT);
    Serial.println("Haut-Parleur I2S (MAX98357A) prêt.");
}

void Audio_I2S::uninstallMic() {
    i2s_driver_uninstall(I2S_MIC_PORT);
}

void Audio_I2S::uninstallSpeaker() {
    i2s_driver_uninstall(I2S_SPK_PORT);
}

size_t Audio_I2S::readMic(int16_t *buffer, size_t bufferSize) {
    size_t bytesRead = 0;
    if (!_raw_buf) return 0;

    i2s_read(I2S_MIC_PORT, _raw_buf, 1024 * 4, &bytesRead, portMAX_DELAY);

    int samplesRead = bytesRead / 4;
    for (int i = 0; i < samplesRead; i++) {
        int32_t val = _raw_buf[i] >> 14;
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        buffer[i] = (int16_t)val;
    }
    return samplesRead * 2;
}

void Audio_I2S::writeSpeaker(const uint8_t *buffer, size_t bufferSize) {
    size_t bytesWritten = 0;
    i2s_write(I2S_SPK_PORT, buffer, bufferSize, &bytesWritten, portMAX_DELAY);
}
