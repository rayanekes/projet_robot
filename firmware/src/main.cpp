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
#include "gui_spotify.h"

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
GuiSpotify*  spotifyUi = nullptr;

QueueHandle_t emotionQueue;
QueueHandle_t commandQueue;
QueueHandle_t audioTxQueue;
QueueHandle_t audioRxQueue;

enum SystemState { SPOTIFY_UI, BACKGROUND_MP3, ONLINE_AI };
volatile SystemState currentState = ONLINE_AI; // Démarre sur l'UI Spotify
bool isMp3ModeInitialized = false;
bool isSpotifyUiInitialized = false;
bool isFaceInitialized = false;
bool hasAutoClosedAtBoot = false;
String lastPlayedFile = "";
uint32_t lastPlayedPosition = 0;
String currentEmotion = "neutre";
bool isSpeaking = false;
std::atomic<bool> spk_ready{false};

const uint16_t ws_server_port = 8765;

// --- Recherche de musique ---
void search_and_play(String query) {
  query.toLowerCase();

  // Algorithme O(1) RAM pour choix aléatoire
  File root = SD.open("/music");
  if (!root) {
      Serial.println("[PLAYER] Erreur : Dossier /music introuvable.");
      return;
  }
  
  // Passe 1 : Compter le nombre de correspondances
  int matchCount = 0;
  while (File file = root.openNextFile()) {
      if (file.isDirectory()) continue;
      String name = String(file.name());
      name.toLowerCase();
      if (name.indexOf(query) != -1) {
          matchCount++;
      }
  }
  root.close();

  if (matchCount == 0) {
      Serial.println("[PLAYER] Titre non trouvé : " + query);
      return;
  }

  // Choisir un index aléatoire parmi les correspondances
  int targetIndex = random(0, matchCount);

  // Passe 2 : Retrouver ce fichier spécifique
  root = SD.open("/music");
  String targetFile = "";
  int currentIndex = 0;

  while (File file = root.openNextFile()) {
      if (file.isDirectory()) continue;
      String name = String(file.name());
      name.toLowerCase();
      if (name.indexOf(query) != -1) {
          if (currentIndex == targetIndex) {
              targetFile = "/music/" + String(file.name());
              break;
          }
          currentIndex++;
      }
  }
  root.close();
  
  if (targetFile != "") {
      Serial.println("[PLAYER] Trouvé (" + String(targetIndex+1) + "/" + String(matchCount) + ") : " + targetFile);
      lastPlayedFile = targetFile;
      lastPlayedPosition = 0; // Nouvelle lecture
      if (currentState != BACKGROUND_MP3) currentState = BACKGROUND_MP3;
      mp3Player->play(targetFile.c_str());
  }
}

// Callback de fin de musique
void audio_eof_mp3(const char *info) {
    Serial.println("[PLAYER] Fin de la musique. Retour à l'IA.");
    lastPlayedFile = "";
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
            currentState == ONLINE_AI ? "AI" : (currentState == BACKGROUND_MP3 ? "BG_MP3" : "UI_MP3"));
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
                currentState = BACKGROUND_MP3;
            } else if (cmd == "stop_mp3") {
                if (isMp3ModeInitialized) {
                    mp3Player->stop();
                    mp3Player->deinit();
                    isMp3ModeInitialized = false;
                }
                currentState = ONLINE_AI;
            } else if (cmd == "pause_mp3") {
                if (isMp3ModeInitialized) {
                    if (mp3Player->isPlaying()) {
                        lastPlayedPosition = mp3Player->getAudioCurrentTime();
                    }
                    mp3Player->stop();
                    mp3Player->deinit(); // Libère le driver I2S (I2S_NUM_1) pour l'IA
                    isMp3ModeInitialized = false;
                }
            } else if (cmd == "resume_mp3") {
                // On simule une reprise en relançant le dernier titre (le lecteur I2S ne gère pas bien le "vrai" resume après deinit de l'I2S)
                if (lastPlayedFile != "") {
                    currentState = BACKGROUND_MP3;
                }
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
        // Si l'UI Spotify est affichée, suspendre le micro (sauf si on ajoute un bouton vocal dessus plus tard).
        // Si mp3Player est en train de jouer en arrière-plan, suspendre le micro pour ne pas capter la musique.
        bool suspendMic = (currentState == SPOTIFY_UI) || (!network->isConnected()) || (isMp3ModeInitialized && mp3Player->isPlaying());

        if (suspendMic) { vTaskDelay(100 / portTICK_PERIOD_MS); continue; }
        // En mode MP3, on suspend le micro
        if (currentState == BACKGROUND_MP3 || !network->isConnected()) { vTaskDelay(100 / portTICK_PERIOD_MS); continue; }

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
        // Si on est en mode UI Spotify ou que le mp3 est initialisé (musique en cours ou prête),
        // on vide la file TTS pour éviter la collision I2S (le mp3 a la priorité audio).
        // Mais si on est en pause (isMp3ModeInitialized = false), l'IA PEUT répondre.
        if (currentState == SPOTIFY_UI || isMp3ModeInitialized) {
            if (xQueueReceive(audioTxQueue, &txChunk, 100) == pdPASS && txChunk.data) free(txChunk.data);
            continue;
        }

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
    
    // spotifyUi = new GuiSpotify();
    face = new FaceRenderer();
    
    // Configuration du tactile
    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    display->getTftPointer()->setTouch(calData);
    
    // On démarre dans l'état configuré au boot (SPOTIFY_UI)
    SystemState lastState = currentState;
    // spotifyUi->init(display->getTftPointer());
    // spotifyUi->buildInterface();
    isSpotifyUiInitialized = false;

    uint32_t lastTick = millis();
    unsigned long lastBlink = millis();
    bool blinkActive = false;

    for (;;) {
        uint32_t delta = millis() - lastTick;
        lastTick = millis();

        // On vérifie s'il faut passer à ONLINE_AI quand on est connecté, depuis l'UI par défaut, une seule fois
        if (!hasAutoClosedAtBoot && currentState == SPOTIFY_UI && network->isConnected()) {
            if (!isMp3ModeInitialized || !mp3Player->isPlaying()) {
                hasAutoClosedAtBoot = true;
                currentState = ONLINE_AI;
            }
        }

        // Switch de mode
        if (currentState != lastState) {
            // Désallocation de l'ancien état
            if (lastState == SPOTIFY_UI) {
                if (isSpotifyUiInitialized) {
                    // spotifyUi->deinit();
                    isSpotifyUiInitialized = false;
                }
            } else if (lastState == ONLINE_AI || lastState == BACKGROUND_MP3) {
                if (isFaceInitialized) {
                    face->deinit();
                    isFaceInitialized = false;
                }
            }

            // Allocation du nouvel état
            if (currentState == SPOTIFY_UI) {
                if (spk_ready.load()) { audio->uninstallSpeaker(); spk_ready.store(false); }
                digitalWrite(MAX_SD_MODE_PIN, LOW); // Mute HP IA
                if (!isSpotifyUiInitialized) {
                    // spotifyUi->init(display->getTftPointer());
                    // spotifyUi->buildInterface();
                    isSpotifyUiInitialized = true;
                }
                if (!isMp3ModeInitialized) { mp3Player->init(); isMp3ModeInitialized = true; }
            } else if (currentState == ONLINE_AI) {
                // On ne deinit pas si on est simplement en pause, on veut pouvoir reprendre (resume).
                // L'audio deinit n'est pas appelé si mp3Player est en pause, on perdrait la position I2S.
                digitalWrite(MAX_SD_MODE_PIN, HIGH); // Unmute HP IA
                vTaskDelay(100 / portTICK_PERIOD_MS);
                if (!spk_ready.load()) { audio->initSpeaker(); spk_ready.store(true); }

                if (!isFaceInitialized) {
                    face->init(display->getTftPointer());
                    isFaceInitialized = true;
                }
                face->setEmotion(FaceEmotion::NEUTRE);
            } else if (currentState == BACKGROUND_MP3) {
                if (spk_ready.load()) { audio->uninstallSpeaker(); spk_ready.store(false); }
                digitalWrite(MAX_SD_MODE_PIN, LOW); // Mute HP IA

                if (!isFaceInitialized) {
                    face->init(display->getTftPointer());
                    isFaceInitialized = true;
                }
                if (!isMp3ModeInitialized) {
                    mp3Player->init();
                    isMp3ModeInitialized = true;
                    // On relance la musique s'il y en avait une (Resume)
                    if (lastPlayedFile != "") {
                        mp3Player->play(lastPlayedFile.c_str());
                    }
                }
                face->setEmotion(FaceEmotion::MUSIQUE); // Visage musique bleu pendant la musique
            }
            lastState = currentState;
        }

        // Gestion du Tactile (Global)
        uint16_t tx=0, ty=0;
        bool touched = display->getTftPointer()->getTouch(&tx, &ty);

        if (currentState == BACKGROUND_MP3 && touched) {
            Serial.println("[TACTILE] Arrêt de la musique demandé !");
            if (isMp3ModeInitialized) {
                if (mp3Player->isPlaying()) {
                    lastPlayedPosition = mp3Player->getAudioCurrentTime();
                }
                mp3Player->stop();
                mp3Player->deinit();
                isMp3ModeInitialized = false;
            }
            currentState = ONLINE_AI;
            vTaskDelay(500 / portTICK_PERIOD_MS); // Anti-rebond et temps pour laisser l'I2S respirer
        } else if (currentState == ONLINE_AI && touched) {
            Serial.println("[TACTILE] Ouverture Spotify demandée !");
            // currentState = SPOTIFY_UI;
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        if (currentState == SPOTIFY_UI) {
            if (isMp3ModeInitialized) {
                mp3Player->loop();
                if (mp3Player->isPlaying()) {
                    // spotifyUi->updateProgress(mp3Player->getAudioCurrentTime(), mp3Player->getAudioFileDuration());
                }
            }

            /*
            if (spotifyUi->isQuitBtnClicked) {
                spotifyUi->isQuitBtnClicked = false;
                currentState = ONLINE_AI;
            }
            if (spotifyUi->isPlayBtnClicked) {
                spotifyUi->isPlayBtnClicked = false;
                if (isMp3ModeInitialized) {
                    mp3Player->pause();
                    spotifyUi->togglePlayPauseIcon(mp3Player->isPlaying());
                }
            }
            */

            lv_tick_inc(delta > 50 ? 16 : delta);
            lv_task_handler();
            vTaskDelay(10 / portTICK_PERIOD_MS);

        } else if (currentState == BACKGROUND_MP3) {
            if (isMp3ModeInitialized) mp3Player->loop();
            if (isFaceInitialized) face->tick(delta > 50 ? 16 : delta); // Animer le visage pendant la musique
            lv_tick_inc(delta > 50 ? 16 : delta);
            lv_task_handler(); // INDISPENSABLE pour que LVGL dessine à l'écran
            vTaskDelay(10 / portTICK_PERIOD_MS);

        } else {
            // ONLINE_AI
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
