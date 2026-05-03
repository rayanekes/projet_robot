// main.cpp - Hub Central du Robot IA (v4.0 - Face Only, No Spotify UI)
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <atomic>
#include <WiFi.h>
#include "secrets.h"
#include "display_tft.h"
#include "audio_i2s.h"
#include "network_ws.h"
#include "audio_chunk.h"
#include "face_renderer.h"

#define MAX_SD_MODE_PIN 15
#define MAX_DIN_PIN     12
#define TFT_RST_PIN     4
#define TFT_CS_PIN      14
#define TOUCH_CS_PIN    13
#define SD_CS_PIN       5

// --- Instances ---
TFT_Display* display   = nullptr;
Audio_I2S*   audio     = nullptr;
Network_WS*  network   = nullptr;
FaceRenderer* face     = nullptr;

QueueHandle_t emotionQueue;
QueueHandle_t commandQueue;
QueueHandle_t audioTxQueue;
QueueHandle_t audioRxQueue;

bool isFaceInitialized = false;
String currentEmotion = "neutre";
bool isSpeaking = false;
std::atomic<bool> spk_ready{false};

const uint16_t ws_server_port = 8765;

// ==========================================
// TÂCHES FREERTOS
// ==========================================

void debugTask(void *pvParameters) {
    for(;;) {
        Serial.printf("[DEBUG] Heap: %u | Min: %u | State: AI\n",
            esp_get_free_heap_size(), 
            esp_get_minimum_free_heap_size());
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void orchestratorTask(void *pvParameters) {
    char cmdBuf[32];
    for(;;) {
        if (xQueueReceive(commandQueue, &cmdBuf, portMAX_DELAY) == pdPASS) {
            String cmd = String(cmdBuf);
            // Plus de commandes spécifiques à la musique
        }
    }
}

void networkTask(void *pvParameters) {
  esp_task_wdt_add(NULL);
  
  IPAddress ip_h = {10, 42, 0, 100};
  IPAddress gate_h = {10, 42, 0, 1};
  IPAddress ip_b = {192, 168, 11, 100};
  IPAddress gate_b = {192, 168, 11, 1};
  IPAddress sub = {255, 255, 255, 0};
  
  bool connected = false;
  Serial.println("Recherche Wi-Fi...");
  WiFi.mode(WIFI_STA); // Save SRAM by disabling AP mode
  WiFi.begin(WIFI_SSID_HOTSPOT, WIFI_PASS_HOTSPOT); 
  WiFi.config(ip_h, gate_h, sub);
  
  for(int i=0; i<16; i++) {
    if(WiFi.status() == WL_CONNECTED) { connected = true; break; }
    vTaskDelay(500 / portTICK_PERIOD_MS); Serial.print(".");
  }
  if(!connected) {
    WiFi.disconnect(); vTaskDelay(100 / portTICK_PERIOD_MS);
    WiFi.begin(WIFI_SSID_BOX, WIFI_PASS_BOX); 
    WiFi.config(ip_b, gate_b, sub);
    while(WiFi.status() != WL_CONNECTED) { vTaskDelay(500 / portTICK_PERIOD_MS); Serial.print("."); }
  }

  String currentIP = WiFi.localIP().toString();
  const char* targetIP = currentIP.startsWith("10.42.") ? "10.42.0.1" : "192.168.11.113";
  network->initWebSocket(targetIP, ws_server_port);

  AudioChunk rxChunk;

  for (;;) {
    esp_task_wdt_reset();
    network->loop();

    if (xQueueReceive(audioRxQueue, &rxChunk, 0) == pdPASS) {
      if (rxChunk.data) {
        network->sendAudio(rxChunk.data, rxChunk.length);
        free(rxChunk.data);
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void micTask(void *pvParameters) {
    audio->initMic();
    static int16_t staticMicBuffer[2048];
    for (;;) {
        if (!network->isConnected()) { vTaskDelay(100 / portTICK_PERIOD_MS); continue; }

        size_t r = audio->readMic(staticMicBuffer, 2048);

        if (r > 0) {
            // Anti-Lag : si la queue est pleine, on supprime le plus vieux paquet pour insérer le nouveau.
            // On privilégie TOUJOURS l'audio le plus récent pour éviter que le retard (lag) ne s'accumule.
            if (uxQueueSpacesAvailable(audioRxQueue) == 0) {
                AudioChunk oldChunk;
                if (xQueueReceive(audioRxQueue, &oldChunk, 0) == pdPASS) {
                    if (oldChunk.data) free(oldChunk.data);
                }
            }

            int16_t* heapBuf = (int16_t*)malloc(r);
            if (heapBuf) {
                memcpy(heapBuf, staticMicBuffer, r);
                AudioChunk c = {(uint8_t*)heapBuf, r};
                if (xQueueSend(audioRxQueue, &c, 0) != pdPASS) free(heapBuf);
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void speakerTask(void *pvParameters) {
    AudioChunk txChunk;
    for (;;) {
        if (xQueueReceive(audioTxQueue, &txChunk, 100) == pdPASS && txChunk.data) {
            if (!spk_ready.load()) { digitalWrite(MAX_SD_MODE_PIN, HIGH); audio->initSpeaker(); spk_ready.store(true); }
            if (face && face->isInitialized()) face->setAudioRMS(computeRMS(txChunk.data, txChunk.length));
            audio->writeSpeaker(txChunk.data, txChunk.length);
            free(txChunk.data);
        }
    }
}

void displayTask(void *pvParameters) {
    display->init();
    lv_init();
    
    face = new FaceRenderer();
    
    // Configuration du tactile
    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    display->getTftPointer()->setTouch(calData);
    
    uint32_t lastTick = millis();
    unsigned long lastBlink = millis();
    bool blinkActive = false;

    // Initialisation IA directe
    digitalWrite(MAX_SD_MODE_PIN, HIGH); // Unmute HP IA
    vTaskDelay(100 / portTICK_PERIOD_MS);
    if (!spk_ready.load()) { audio->initSpeaker(); spk_ready.store(true); }

    if (!isFaceInitialized) {
        face->init(display->getTftPointer());
        isFaceInitialized = true;
    }
    face->setEmotion(FaceEmotion::NEUTRE);

    for (;;) {
        uint32_t delta = millis() - lastTick;
        lastTick = millis();

        // Gestion du Tactile (Global)
        uint16_t tx=0, ty=0;
        bool touched = display->getTftPointer()->getTouch(&tx, &ty);

        if (touched) {
            // Touched logic si nécessaire
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        unsigned long now = millis();
        if (!blinkActive && now - lastBlink > 3000) { blinkActive = true; lastBlink = now; }
        if (blinkActive && now - lastBlink > 200) { blinkActive = false; }

        char receivedEmotion[32];
        if (xQueueReceive(emotionQueue, &receivedEmotion, 0) == pdPASS) {
            String em = String(receivedEmotion);
            if (em == "parle") { isSpeaking = true; face->setEmotion(FaceEmotion::PARLE); }
            else if (em == "idle") { isSpeaking = false; face->setEmotion(FaceEmotion::NEUTRE); }
            else if (!isSpeaking) face->setEmotionFromString(em);
        }

        if (isFaceInitialized) face->tick(delta > 50 ? 16 : delta);
        lv_tick_inc(delta > 50 ? 16 : delta);
        lv_task_handler(); // INDISPENSABLE pour que LVGL dessine à l'écran
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    esp_task_wdt_init(30, true);
    
    // Mute immédiat au boot
    pinMode(MAX_SD_MODE_PIN, OUTPUT); digitalWrite(MAX_SD_MODE_PIN, LOW);
    
    pinMode(TFT_CS_PIN, OUTPUT); digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(TOUCH_CS_PIN, OUTPUT); digitalWrite(TOUCH_CS_PIN, HIGH);
    pinMode(SD_CS_PIN, OUTPUT); digitalWrite(SD_CS_PIN, HIGH);
    pinMode(TFT_RST_PIN, OUTPUT); digitalWrite(TFT_RST_PIN, HIGH);
    
    SPI.begin(18, 19, 23, 5);
    int retry = 0;
    while (!SD.begin(5, SPI, 4000000) && retry < 3) { retry++; delay(500); }
    
    display = new TFT_Display();
    audio = new Audio_I2S();
    network = new Network_WS();

    emotionQueue = xQueueCreate(5, 32);
    commandQueue = xQueueCreate(10, 32);
    audioTxQueue = xQueueCreate(10, sizeof(AudioChunk));
    audioRxQueue = xQueueCreate(10, sizeof(AudioChunk));

    xTaskCreatePinnedToCore(networkTask, "Net", 8192, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(orchestratorTask, "Orc", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(displayTask, "Disp", 10240, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(micTask, "Mic", 6144, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(speakerTask, "Spk", 6144, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(debugTask, "Debug", 2048, NULL, 0, NULL, 0);
}

void loop() { vTaskDelete(NULL); }
