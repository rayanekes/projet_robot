#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>

// On importe vos identifiants Wi-Fi directement depuis le robot
#include "../../../firmware/include/secrets.h" 

// ── Configuration Serveur Python ────────────────────────
const char* server_ip = "192.168.11.113"; // Remplacez si l'IP de votre PC a changé
const uint16_t server_port = 8765;

// ── Pins I2S — MAX98357A ────────────────────────────────────
#define I2S_BCLK    17
#define I2S_LRC     18
#define I2S_DOUT    21

WebSocketsClient webSocket;

void initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 24000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        // Passage au canal Droit pour éviter le bug de bit-shift du canal gauche
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 16,
        .dma_buf_len = 1024,
        .use_apll = true, // APLL = Meilleure horloge audio
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_LRC,
        .data_out_num = I2S_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_1);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_BIN) {
        size_t bytes_written;
        // En mode ONLY_RIGHT, l'I2S attend directement du mono (1 sample = 16 bits).
        // On envoie le payload brut sans le dupliquer en stéréo !
        i2s_write(I2S_NUM_1, payload, length, &bytes_written, portMAX_DELAY);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== Test Wi-Fi Audio (Onde Pure 24000Hz) ===");

    initI2S();

    WiFi.begin(WIFI_SSID_BOX, WIFI_PASS_BOX);
    Serial.print("Connexion Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ Wi-Fi connecté ! IP: " + WiFi.localIP().toString());

    webSocket.begin(server_ip, server_port, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(2000);
    Serial.println("Recherche du serveur Python...");
}

void loop() {
    webSocket.loop();
}
