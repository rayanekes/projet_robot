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
#include "audio_mp3.h"
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
AudioMP3*    mp3Player = nullptr;
FaceRenderer* face     = nullptr;

QueueHandle_t emotionQueue;
QueueHandle_t commandQueue;
QueueHandle_t audioTxQueue;
QueueHandle_t audioRxQueue;

enum SystemState { OFFLINE_MP3, ONLINE_AI };
volatile SystemState currentState = ONLINE_AI; // Démarre en mode IA
bool isMp3ModeInitialized = false;
String currentEmotion = "neutre";
bool isSpeaking = false;
std::atomic<bool> spk_ready{false};

const uint16_t ws_server_port = 8765;

// --- Recherche de musique ---
void search_and_play(String query) {
  query.toLowerCase();
  File root = SD.open("/music");
  if (!root) {
      Serial.println("[PLAYER] Erreur : Dossier /music introuvable.");
      return;
  }
  
  String targetFile = "";
  while (File file = root.openNextFile()) {
      String name = String(file.name());
      name.toLowerCase();
      if (name.indexOf(query) != -1) {
          targetFile = "/music/" + String(file.name());
          break;
      }
  }
  
  if (targetFile != "") {
      Serial.println("[PLAYER] Trouvé : " + targetFile);
      if (currentState != OFFLINE_MP3) currentState = OFFLINE_MP3;
      mp3Player->play(targetFile.c_str());
  } else {
      Serial.println("[PLAYER] Titre non trouvé : " + query);
  }
}

// Callback de fin de musique
void audio_eof_mp3(const char *info) {
    Serial.println("[PLAYER] Fin de la musique. Retour à l'IA.");
    currentState = ONLINE_AI;
}

// ==========================================
// TÂCHES FREERTOS
// ==========================================

void debugTask(void *pvParameters) {
    for(;;) {
        Serial.printf("[DEBUG] Heap: %u | Min: %u | State: %s\n", 
            esp_get_free_heap_size(), 
            esp_get_minimum_free_heap_size(),
            currentState == ONLINE_AI ? "AI" : "MP3");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void orchestratorTask(void *pvParameters) {
    char cmdBuf[128];
    for(;;) {
        if (xQueueReceive(commandQueue, &cmdBuf, portMAX_DELAY) == pdPASS) {
            StaticJsonDocument<256> doc;
            deserializeJson(doc, cmdBuf);
            String cmd = doc["command"] | "none";
            
            if (cmd == "play_song") {
                String title = doc["title"] | "";
                if (title != "") search_and_play(title);
            } else if (cmd == "start_mp3") {
                currentState = OFFLINE_MP3;
            } else if (cmd == "stop_mp3") {
                currentState = ONLINE_AI;
            }
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
  unsigned long lastConnCheck = millis();
  uint32_t backoff = 2000;

  for (;;) {
    esp_task_wdt_reset();
    network->loop();
    if (xQueueReceive(audioRxQueue, &rxChunk, 0) == pdPASS) {
      if (rxChunk.data) { network->sendAudio(rxChunk.data, rxChunk.length); free(rxChunk.data); }
    }
    if (millis() - lastConnCheck > backoff) {
      if (!network->isConnected()) { 
          network->initWebSocket(targetIP, ws_server_port); 
          backoff = std::min(backoff * 2, (uint32_t)60000);
      } else { backoff = 2000; }
      lastConnCheck = millis();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void micTask(void *pvParameters) {
    audio->initMic();
    static int16_t staticMicBuffer[2048];
    for (;;) {
        // En mode MP3, on suspend le micro
        if (currentState == OFFLINE_MP3 || !network->isConnected()) { vTaskDelay(100 / portTICK_PERIOD_MS); continue; }
        size_t r = audio->readMic(staticMicBuffer, 2048);
        if (r > 0 && uxQueueSpacesAvailable(audioRxQueue) > 2) {
            int16_t* heapBuf = (int16_t*)malloc(r);
            if (heapBuf) { memcpy(heapBuf, staticMicBuffer, r); AudioChunk c = {(uint8_t*)heapBuf, r}; if (xQueueSend(audioRxQueue, &c, 0) != pdPASS) free(heapBuf); }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void speakerTask(void *pvParameters) {
    AudioChunk txChunk;
    for (;;) {
        if (currentState == OFFLINE_MP3) { if (xQueueReceive(audioTxQueue, &txChunk, 100) == pdPASS && txChunk.data) free(txChunk.data); continue; }
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
    face->init(display->getTftPointer());
    
    // Configuration du tactile pour arrêter la musique
    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    display->getTftPointer()->setTouch(calData);
    
    SystemState lastState = ONLINE_AI;
    uint32_t lastTick = millis();
    unsigned long lastBlink = millis();
    bool blinkActive = false;

    for (;;) {
        uint32_t delta = millis() - lastTick;
        lastTick = millis();

        // Switch de mode
        if (currentState != lastState) {
            if (currentState == OFFLINE_MP3) {
                if (spk_ready.load()) { audio->uninstallSpeaker(); spk_ready.store(false); }
                digitalWrite(MAX_SD_MODE_PIN, LOW); // Mute HP IA
                mp3Player->init();
                face->setEmotion(FaceEmotion::ECOUTE); // Visage "écoute" vert pendant la musique
                isMp3ModeInitialized = true;
            } else {
                if (isMp3ModeInitialized) { mp3Player->deinit(); isMp3ModeInitialized = false; }
                digitalWrite(MAX_SD_MODE_PIN, HIGH); // Unmute HP IA
                vTaskDelay(100 / portTICK_PERIOD_MS);
                audio->initSpeaker(); spk_ready.store(true);
                face->setEmotion(FaceEmotion::NEUTRE);
            }
            lastState = currentState;
        }

        // Tactile : Si on touche l'écran, on coupe la musique
        uint16_t tx=0, ty=0;
        if (display->getTftPointer()->getTouch(&tx, &ty)) {
            if (currentState == OFFLINE_MP3) {
                Serial.println("[TACTILE] Arrêt de la musique demandé !");
                currentState = ONLINE_AI;
                vTaskDelay(200 / portTICK_PERIOD_MS); // Anti-rebond
            }
        }

        if (currentState == OFFLINE_MP3) {
            mp3Player->loop(); 
            face->tick(delta > 50 ? 16 : delta); // Animer le visage pendant la musique
            lv_task_handler(); // INDISPENSABLE pour que LVGL dessine à l'écran
            vTaskDelay(5 / portTICK_PERIOD_MS);
        } else {
            // Animation des yeux
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
            
            // Simuler le clignement
            if (blinkActive && !isSpeaking && currentEmotion == "neutre") {
               // Pourrait changer le dessin de l'oeil ici, mais on laisse le renderer gérer
            }

            face->tick(delta > 50 ? 16 : delta);
            lv_task_handler(); // INDISPENSABLE pour que LVGL dessine à l'écran
            vTaskDelay(8 / portTICK_PERIOD_MS);
        }
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
    mp3Player = new AudioMP3();

    emotionQueue = xQueueCreate(5, 32);
    commandQueue = xQueueCreate(10, 128);
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
