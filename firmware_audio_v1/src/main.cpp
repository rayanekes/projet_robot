#include <Arduino.h>
#include <driver/i2s.h>

// ── Configuration UART (Réception depuis S3) ───────────────
#define UART_RX_PIN 16
#define UART_BAUD   921600
#define RX_BUF_SIZE 131072 // 128 Ko : Exploite une grande partie de la RAM dispo de la V1

// ── Configuration I2S (MAX98357A) ──────────────────────────
#define I2S_BCLK    27
#define I2S_LRC     26
#define I2S_DOUT    25

#define CHUNK_SIZE  1024
uint8_t audio_buf[CHUNK_SIZE];

void initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 24000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false, // V1 n'a pas besoin de l'APLL pour être stable
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void setup() {
    // Port de debug classique (vers l'ordinateur)
    Serial.begin(115200);
    Serial.println("\n[V1 Audio Slave] Démarrage...");

    // Initialisation de l'UART2 (vers la S3) avec un énorme buffer
    Serial2.setRxBufferSize(RX_BUF_SIZE);
    Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);
    Serial.println("[V1 Audio Slave] UART2 Prêt à 921600 bauds sur GPIO 16");

    initI2S();
    Serial.println("[V1 Audio Slave] I2S Prêt. En attente de son...");
}

void loop() {
    // On lit autant de données que possible depuis la S3
    size_t available = Serial2.available();
    if (available > 0) {
        size_t bytes_to_read = min(available, (size_t)CHUNK_SIZE);
        size_t len = Serial2.readBytes(audio_buf, bytes_to_read);
        
        if (len > 0) {
            size_t bytes_written;
            // i2s_write bloquera automatiquement si le DMA I2S est plein,
            // imposant ainsi le tempo matériel (24000Hz).
            i2s_write(I2S_NUM_0, audio_buf, len, &bytes_written, portMAX_DELAY);
        }
    }
}
