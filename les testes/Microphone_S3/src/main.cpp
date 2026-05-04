#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <LittleFS.h>
#include "../../../firmware/include/secrets.h" 

const char* server_ip = "192.168.11.113";
const uint16_t server_port = 8766;

#define I2S_SCK     4
#define I2S_WS      5
#define I2S_SD      6

#define SAMPLE_RATE 16000
#define RECORD_TIME_SEC 20
#define TOTAL_BYTES_MAX (SAMPLE_RATE * RECORD_TIME_SEC * 2) // 640 Ko
#define WAKE_THRESHOLD 3000 

WebSocketsClient webSocket;
int16_t* psram_audio_buffer = nullptr;
bool is_connected = false;

// Lit le compteur persistant dans la mémoire Flash
int getNextFileNumber() {
    int num = 1;
    if (LittleFS.exists("/counter.txt")) {
        File f = LittleFS.open("/counter.txt", FILE_READ);
        if (f) {
            String val = f.readString();
            num = val.toInt() + 1;
            f.close();
        }
    }
    File f = LittleFS.open("/counter.txt", FILE_WRITE);
    f.print(num);
    f.close();
    return num;
}

void initI2SMicro() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };
    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        Serial.println("\n✅ Connecté au serveur Python !");
        is_connected = true;
    } else if (type == WStype_DISCONNECTED) {
        is_connected = false;
    }
}

void waitForWakeWord() {
    Serial.println("\n💤 Mode Veille Hors-Ligne (Écoute...).");
    int16_t chunk[512];
    size_t bytes_read;
    
    while (true) {
        webSocket.loop(); // Permet de se connecter même en veille
        if (is_connected) return; // Si le PC se connecte, on sort de la veille pour vider les fichiers

        i2s_read(I2S_NUM_1, chunk, sizeof(chunk), &bytes_read, portMAX_DELAY);
        
        int64_t sum_squares = 0;
        int samples = bytes_read / 2;
        for (int i = 0; i < samples; i++) {
            // Amplification x8
            int32_t amp = (int32_t)chunk[i] * 8;
            if (amp > 32767) amp = 32767;
            if (amp < -32768) amp = -32768;
            chunk[i] = (int16_t)amp;
            sum_squares += (int32_t)chunk[i] * chunk[i];
        }
        float rms = sqrt(sum_squares / samples);
        
        if (rms > WAKE_THRESHOLD) {
            Serial.println("\n🔔 WAKE WORD DETECTÉ ! Réveil du robot !");
            return;
        }
    }
}

void recordAndSaveToFlash() {
    Serial.println("🔴 Enregistrement de 20s en cours (PSRAM)...");
    size_t bytes_read = 0;
    size_t total_bytes_read = 0;
    
    int16_t chunk[512];
    while (total_bytes_read < TOTAL_BYTES_MAX) {
        i2s_read(I2S_NUM_1, chunk, sizeof(chunk), &bytes_read, portMAX_DELAY);
        int samples = bytes_read / 2;
        for (int i = 0; i < samples; i++) {
            int32_t amp = (int32_t)chunk[i] * 8;
            if (amp > 32767) amp = 32767;
            if (amp < -32768) amp = -32768;
            chunk[i] = (int16_t)amp;
        }
        memcpy((uint8_t*)psram_audio_buffer + total_bytes_read, chunk, bytes_read);
        total_bytes_read += bytes_read;
    }

    int file_num = getNextFileNumber();
    String filename = "/record_" + String(file_num) + ".raw";
    
    Serial.println("🛑 Sauvegarde dans la Flash : " + filename);

    File file = LittleFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("❌ Erreur d'écriture Flash !");
        return;
    }
    file.write((uint8_t*)psram_audio_buffer, TOTAL_BYTES_MAX);
    file.close();
    Serial.println("💾 Fichier sauvegardé !");
}

void sendFilesToPython() {
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    
    bool files_sent = false;

    while (file) {
        String fname = file.name();
        if (fname.startsWith("record_")) {
            Serial.println("\n📡 Transfert de : " + fname);
            
            // On prévient Python du nom du fichier pour sa numérotation
            String header = "FILENAME:" + fname;
            webSocket.sendTXT(header);
            delay(50); // Le temps que Python traite

            size_t bytes_sent = 0;
            uint8_t buf[4096];
            while (file.available()) {
                size_t len = file.read(buf, sizeof(buf));
                webSocket.sendBIN(buf, len);
                bytes_sent += len;
                delay(2);
            }
            
            webSocket.sendBIN((uint8_t*)"EOF", 3);
            Serial.printf("✅ Transfert de %d octets terminé !\n", bytes_sent);
            
            // IMPORTANT : On efface le fichier de la mémoire une fois envoyé
            String fullPath = "/" + fname;
            file.close();
            LittleFS.remove(fullPath);
            Serial.println("🗑️ " + fname + " effacé de la Flash.");
            
            files_sent = true;
            delay(500); // Pause entre chaque fichier
        } else {
            file.close();
        }
        
        root = LittleFS.open("/"); // Réouverture nécessaire après un .remove() sur LittleFS
        file = root.openNextFile();
        while (file && !String(file.name()).startsWith("record_")) {
            file = root.openNextFile();
        }
    }
    
    if (files_sent) {
        Serial.println("🎉 Tous les fichiers hors-ligne ont été vidés vers le PC !");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== S3 OFFLINE RECORDER (Numéroté) ===");
    
    if (!LittleFS.begin(true)) {
        Serial.println("❌ Erreur Montage LittleFS");
        return;
    }

    psram_audio_buffer = (int16_t*)heap_caps_malloc(TOTAL_BYTES_MAX, MALLOC_CAP_SPIRAM);
    initI2SMicro();

    WiFi.begin(WIFI_SSID_BOX, WIFI_PASS_BOX);
    webSocket.begin(server_ip, server_port, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(1000);
}

void loop() {
    webSocket.loop();
    
    if (is_connected) {
        sendFilesToPython();
        // Optionnel : ne rien faire d'autre quand le PC est là, ou continuer d'écouter
    } else {
        waitForWakeWord();
        if (!is_connected) { // Si on s'est réveillé pour le son, et non pour le Wi-Fi
            recordAndSaveToFlash();
        }
    }
}
