#include "audio_chunk.h"
#include "audio_i2s.h"
#include "display_tft.h"
#include "face_renderer.h"
#include "network_ws.h"    // Inclut aussi la macro REMOTELOG
#include "ir_remote.h"     // Télécommande infrarouge
#include "secrets.h"
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <atomic>
#include <esp_task_wdt.h>

// --- Définition des broches (ESP32-S3 N16R8 — GPIO Matrix) ---
#define TFT_RST_PIN 13     // Reset écran ST7789
#define TFT_CS_PIN 10      // CS écran
#define TFT_BL_PIN 14      // Backlight écran (LED pin)

// --- Instances ---
TFT_Display  *display = nullptr;
Audio_I2S    *audio   = nullptr;
Network_WS   *network = nullptr;
FaceRenderer *face    = nullptr;
IRRemote      ir;             // Télécommande IR (déclarée avant setup)

// --- Queues FreeRTOS ---
QueueHandle_t emotionQueue;
QueueHandle_t commandQueue;
QueueHandle_t audioTxQueue;
QueueHandle_t audioRxQueue;

// --- État global ---
bool isFaceInitialized = false;
String currentEmotion = "neutre";
bool isSpeaking = false;

bool irSleepMode = false;   // Mode veille IR (mute ampli + visage veille)

// Volume IR (0-100) appliqué localement via I2S gain numérique
// NOTE: La gestion du volume est faite logiciellement dans audio_i2s.cpp (Scaling 8 Ohms).
int irVolume = 80;  // Niveau de départ 80%

// Niveau de luminosité écran (0-255 via PWM sur TFT_BL_PIN)
int tftBrightness = 255;

// Port WebSocket du serveur Python
const uint16_t ws_server_port = 8765;

// ==========================================
// TÂCHE DEBUG — Surveillance mémoire (Core 0)
// ==========================================
void debugTask(void *pvParameters) {
  for (;;) {
    uint32_t freeHeap = esp_get_free_heap_size();
    uint32_t minHeap  = esp_get_minimum_free_heap_size();
    Serial.printf("[DEBUG] Heap libre: %u B | Min historique: %u B\n",
                  freeHeap, minHeap);
    REMOTELOG("Heap libre: %u B | Min: %u B", freeHeap, minHeap);
    vTaskDelay(15000 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// TÂCHE COMMANDES — Commandes IR locales (Core 0)
// ==========================================
void commandTask(void *pvParameters) {
  char cmd[128];
  for (;;) {
    if (xQueueReceive(commandQueue, &cmd, 100) == pdPASS) {
      Serial.printf("[CMD] Traitement : %s\n", cmd);

      // ── Volume (le serveur s'en charge, on log juste)
      if (strncmp(cmd, "cmd:vol:", 8) == 0) {
        const char* val = cmd + 8;
        if (val[0] == '+')      irVolume = min(100, irVolume + atoi(val+1));
        else if (val[0] == '-') irVolume = max(0,   irVolume - atoi(val+1));
        else                    irVolume = max(0, min(100, atoi(val)));
        Serial.printf("[CMD] Volume local : %d%%\n", irVolume);
      }

      // ── Luminosité écran via PWM
      else if (strncmp(cmd, "cmd:bright:", 11) == 0) {
        const char* val = cmd + 11;
        if (val[0] == '+')      tftBrightness = min(255, tftBrightness + atoi(val+1) * 255 / 100);
        else if (val[0] == '-') tftBrightness = max(10,  tftBrightness - atoi(val+1) * 255 / 100);
        else                    tftBrightness = max(0, min(255, atoi(val)));
        ledcWrite(TFT_BL_PIN, tftBrightness);  // Core v3 API: pin au lieu de canal
        Serial.printf("[CMD] Luminosité écran : %d/255\n", tftBrightness);
      }

      // ── Changement de langue : feedback visuel (déjà géré via emotionQueue)
      else if (strncmp(cmd, "cmd:lang:", 9) == 0) {
        const char* lang = cmd + 9;
        Serial.printf("[CMD] Langue activée côté Python : %s\n", lang);
        // Le retour visuel est déjà envoyé via emotionQueue depuis handleJsonMessage
      }

      // ── Réinitialisation : visage neutre
      else if (strcmp(cmd, "cmd:reset") == 0) {
        if (emotionQueue) {
          char em[32] = "neutre";
          xQueueSend(emotionQueue, em, 0);
        }
      }

      // ── Stop TTS : déjà géré par interrupt_flag côté Python
      else if (strcmp(cmd, "cmd:stop") == 0) {
        // Vider la queue audio en cours immédiatement
        AudioChunk staleChunk;
        while (xQueueReceive(audioTxQueue, &staleChunk, 0) == pdPASS) {
          if (staleChunk.data) free(staleChunk.data);
        }
        Serial.println("[CMD] TTS stoppé, queue audio vidée");
      }
    }
  }
}

// ==========================================
// TÂCHE RÉSEAU — WiFi + WebSocket (Core 0)
// ==========================================
void networkTask(void *pvParameters) {

  // Adresses IP statiques (réduit la latence DHCP)
  IPAddress ip_hotspot = {10, 42, 0, 100};
  IPAddress gate_hotspot = {10, 42, 0, 1};
  IPAddress ip_box = {192, 168, 11, 100};
  IPAddress gate_box = {192, 168, 11, 1};
  IPAddress subnet = {255, 255, 255, 0};

  WiFi.mode(WIFI_STA);
  Serial.println("[WiFi] Recherche du réseau Hotspot...");
  WiFi.begin(WIFI_SSID_HOTSPOT, WIFI_PASS_HOTSPOT);
  WiFi.config(ip_hotspot, gate_hotspot, subnet);

  bool connected = false;
  for (int i = 0; i < 16; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
  }

  if (!connected) {
    Serial.println("\n[WiFi] Hotspot indisponible, essai sur Box...");
    WiFi.disconnect();
    vTaskDelay(200 / portTICK_PERIOD_MS);
    WiFi.begin(WIFI_SSID_BOX, WIFI_PASS_BOX);
    WiFi.config(ip_box, gate_box, subnet);

    uint32_t timeout = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      vTaskDelay(500 / portTICK_PERIOD_MS);
      Serial.print(".");
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] ERREUR: Impossible de se connecter !");
    // Redémarrer après 5s pour réessayer
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();
  }

  Serial.printf("\n[WiFi] Connecté ! IP locale: %s\n",
                WiFi.localIP().toString().c_str());

  // Détecter automatiquement l'IP du serveur selon le réseau
  String localIP = WiFi.localIP().toString();
  const char *targetIP =
      localIP.startsWith("10.42.") ? "10.42.0.1" : "192.168.11.113";
  Serial.printf("[WS] Cible serveur: %s:%d\n", targetIP, ws_server_port);

  network->initWebSocket(targetIP, ws_server_port);

  // ─── Plan B #4 : Initialisation OTA (après WiFi connecté) ─────────────
  // Permet de flasher le firmware sans câble USB via Arduino IDE / PlatformIO
  // La tâche otaTask appellera ArduinoOTA.handle() régulièrement sur Core 0.
  ArduinoOTA.setHostname("robot-esp32");     // Identifiant réseau
  ArduinoOTA.setPassword("robot1234");       // Mot de passe de sécurité OTA
  ArduinoOTA
    .onStart([]() {
      Serial.println("[OTA] Début du flashage...");
    })
    .onEnd([]() {
      Serial.println("\n[OTA] Flashage terminé — redémarrage.");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("[OTA] Progression: %u%%\r", (progress * 100) / total);
    })
    .onError([](ota_error_t error) {
      Serial.printf("[OTA] Erreur[%u]: ", error);
      if      (error == OTA_AUTH_ERROR)    Serial.println("Auth failed");
      else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed");
      else if (error == OTA_END_ERROR)     Serial.println("End failed");
    });
  ArduinoOTA.begin();
  Serial.printf("[OTA] Prêt. Hostname: robot-esp32 | IP: %s\n",
                WiFi.localIP().toString().c_str());
  // ────────────────────────────────────────────────────────────────────────────

  AudioChunk rxChunk;
  for (;;) {
    network->loop();

    // Envoyer les chunks micro au serveur (s'il y en a dans la queue)
    if (xQueueReceive(audioRxQueue, &rxChunk, 0) == pdPASS) {
      if (rxChunk.data) {
        network->sendAudio(rxChunk.data, rxChunk.length);
        free(rxChunk.data);
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// TÂCHE MICROPHONE — Capture I2S (Core 1)
// ==========================================
void micTask(void *pvParameters) {
  audio->initMic();
  // 512 int16 samples = DMA_BUF_LEN(64) × DMA_BUF_COUNT(8)
  static int16_t staticMicBuffer[512];
  bool was_connected = false;

  for (;;) {
    bool now_connected = network->isConnected();

    // Envoyer le statut "ecoute" à l'écran + drainer les vieux chunks audio
    if (now_connected && !was_connected) {
      char em[32] = "ecoute";
      if (emotionQueue != NULL)
        xQueueSend(emotionQueue, &em, 0);
      // Vider la queue audio TX pour ne pas jouer de vieux sons après reconnexion
      AudioChunk staleChunk;
      while (xQueueReceive(audioTxQueue, &staleChunk, 0) == pdPASS) {
        if (staleChunk.data) free(staleChunk.data);
      }
    }
    was_connected = now_connected;

    if (!now_connected) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    size_t bytesRead = audio->readMic(staticMicBuffer, 512);

    if (bytesRead > 0) {
      // Anti-lag : si la queue est pleine, supprimer le paquet le plus ancien
      // pour toujours garder l'audio le plus récent (évite l'accumulation de
      // lag)
      if (uxQueueSpacesAvailable(audioRxQueue) == 0) {
        AudioChunk oldChunk;
        if (xQueueReceive(audioRxQueue, &oldChunk, 0) == pdPASS) {
          if (oldChunk.data)
            free(oldChunk.data);
        }
      }

      int16_t *heapBuf = (int16_t *)malloc(bytesRead);
      if (heapBuf) {
        memcpy(heapBuf, staticMicBuffer, bytesRead);
        AudioChunk c = {(uint8_t *)heapBuf, bytesRead};
        if (xQueueSend(audioRxQueue, &c, 0) != pdPASS) {
          free(heapBuf);
        }
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// TÂCHE AUDIO OUT — I2S direct sur S3 via MAX98357A (Core 1)
// ==========================================
void speakerTask(void *pvParameters) {
  AudioChunk txChunk;

  // Initialiser le haut-parleur MAX98357A via I2S_NUM_1 (nouvelle API)
  audio->initSpeaker();

  for (;;) {
    if (xQueueReceive(audioTxQueue, &txChunk, 100) == pdPASS && txChunk.data) {

      // Mettre à jour l'animation de la bouche (lip-sync)
      if (face && face->isInitialized()) {
        face->setAudioRMS(computeRMS(txChunk.data, txChunk.length));
      }

      // Lecture directe sur l'ampli (avec compensation volume 8Ω)
      audio->writeSpeaker(txChunk.data, txChunk.length);

      free(txChunk.data);
    }
  }
}

// ==========================================
// TÂCHE AFFICHAGE — LVGL + FaceRenderer (Core 1)
// ==========================================
void displayTask(void *pvParameters) {
  display->init();
  lv_init();

  face = new FaceRenderer();

  // NOTE: Tactile XPT2046 supprimé — montage simplifié pour ESP32-S3

  uint32_t lastTick = millis();
  uint32_t lastBlink = millis();
  bool blinkActive = false;

  // NOTE: initSpeaker() est maintenant appelé dans speakerTask (Core 1)
  // car i2s_channel_enable doit être sur le même core que i2s_channel_write.

  // Initialiser le rendu vectoriel du visage
  if (!isFaceInitialized) {
    face->init(display->getTftPointer());
    isFaceInitialized = true;
  }
  face->setEmotion(FaceEmotion::NEUTRE);

  for (;;) {
    uint32_t now = millis();
    uint32_t delta = now - lastTick;
    lastTick = now;

    // Cap delta à 50ms pour éviter les sauts d'animation trop grands
    uint32_t clampedDelta = delta > 50 ? 16 : delta;

    // NOTE: Bloc tactile supprimé — contrôle via IR uniquement

    // Clignement d'état visuel (LED ou indicateur physique optionnel)
    if (!blinkActive && now - lastBlink > 3000) {
      blinkActive = true;
      lastBlink = now;
    }
    if (blinkActive && now - lastBlink > 200) {
      blinkActive = false;
    }

    // Traitement des messages d'émotion depuis la queue
    char receivedEmotion[32];
    if (xQueueReceive(emotionQueue, &receivedEmotion, 0) == pdPASS) {
      String em = String(receivedEmotion);
      if (em == "parle") {
        isSpeaking = true;
        face->setEmotion(FaceEmotion::PARLE);
      } else if (em == "idle") {
        isSpeaking = false;
        face->setEmotion(FaceEmotion::NEUTRE);
      } else if (em == "ecoute") {
        if (!isSpeaking)
          face->setEmotion(FaceEmotion::ECOUTE);
      } else if (!isSpeaking) {
        face->setEmotionFromString(em);
      }
    }

    // Tick LVGL + FaceRenderer (doit être appelé régulièrement)
    if (isFaceInitialized)
      face->tick(clampedDelta);
    lv_tick_inc(clampedDelta);
    lv_task_handler(); // INDISPENSABLE — force le rendu LVGL

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// SETUP — Initialisation au démarrage
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1500); // Délai de sécurité pour le port USB-CDC sur S3
  Serial.println("\n[ROBOT] === Démarrage ===");

  // Watchdog par défaut d'Arduino (Core v3) actif.

  // Pin CS écran haute impédance au boot
  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);
  pinMode(TFT_RST_PIN, OUTPUT);
  digitalWrite(TFT_RST_PIN, HIGH);

  // Backlight ON au démarrage avec PWM (pour contrôle de luminosité)
  ledcAttach(TFT_BL_PIN, 5000, 8); // Core v3 API
  ledcWrite(TFT_BL_PIN, tftBrightness);

  // NOTE: SPI.begin() supprimé — TFT_eSPI initialise le bus SPI automatiquement
  // via les build flags (TFT_SCLK=12, TFT_MISO=-1, TFT_MOSI=11, TFT_CS=10)

  // Allocation dynamique des objets principaux (évite les débordements SRAM au
  // boot)
  display = new TFT_Display();
  audio = new Audio_I2S();
  network = new Network_WS();

  // Création des queues FreeRTOS — ESP32-S3 : queues agrandies (PSRAM 8Mo)
  emotionQueue = xQueueCreate(5, 32);                  // Émotions visage
  commandQueue = xQueueCreate(10, 128);                // Commandes applicatives
  audioTxQueue = xQueueCreate(60, sizeof(AudioChunk)); // Audio TTS → HP (60 = ~2.5s buffer)
  audioRxQueue = xQueueCreate(20, sizeof(AudioChunk)); // Audio Micro → WiFi

  // Lancement des tâches FreeRTOS
  // Priorités : Mic(4) > Spk(5) > Net(3) > Disp(1) > Debug(0)
  // Core 0 : Réseau (non-preemptible par les IRQ Arduino)
  // Core 1 : Audio + Affichage (accès matériel I2S/SPI)
  // ─── Plan B #4 : tâche OTA FreeRTOS ──────────────────────────────────
  // Placement sur Core 0 (même core que le réseau) pour accès WiFi thread-safe.
  // Priorité 1 (minimale) : ne préempte jamais l'audio, l'écran ou le réseau.
  // Stack 4096 : suffisant pour ArduinoOTA.handle() + mémoire TLS interne.
  xTaskCreatePinnedToCore(
    [](void*) {
      // Ne pas ajouter ce thread au watchdog : OTA peut durer plusieurs secondes
      // sans appeler handle() et déclencher un faux reset.
      for (;;) {
        ArduinoOTA.handle();               // Bloquant seulement pendant un flash
        vTaskDelay(500 / portTICK_PERIOD_MS); // Vérifier toutes les 500ms
      }
    },
    "OTA",      // Nom de la tâche (visible dans le moniteur FreeRTOS)
    4096,       // Stack : ArduinoOTA + mDNS interne
    NULL,
    1,          // Priorité minimale
    NULL,
    0           // Core 0 : même que networkTask (WiFi thread-safe)
  );

  xTaskCreatePinnedToCore(networkTask,  "Net",  16384, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(displayTask,  "Disp", 16384, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(micTask,      "Mic",  12288, NULL, 4, NULL, 0);  // Déplacé sur le Core 0 pour équilibrage
  xTaskCreatePinnedToCore(speakerTask,  "Spk",  16384, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(debugTask,    "Dbg",  4096,  NULL, 0, NULL, 0);
  xTaskCreatePinnedToCore(commandTask,  "Cmd",  8192,  NULL, 2, NULL, 0);  // Commandes IR locales

  // ── Tâche IR télécommande (Core 0, priorité basse) ─────────────────────────
  // Poll toutes les 50ms : laténce IR imperceptible, CPU négligeable.
  // Core 0 : partage le core réseau, thread-safe pour WebSocket.
  xTaskCreatePinnedToCore(
    [](void*) {
      ir.begin();
      for (;;) {
        const char* cmd = ir.poll();
        if (cmd != nullptr) {
          Serial.printf("[IR] Commande détectée : %s\n", cmd);

          // Envoi au serveur Python via WebSocket : {"cmd":"cmd:lang:fr"}
          if (network && network->isConnected()) {
            network->sendCmd(cmd);
          }

          // Gestion locale du mode sleep (muet l'ampli sans attendre le serveur)
          if (strcmp(cmd, "cmd:sleep") == 0) {
            irSleepMode = !irSleepMode;
            // NOTE: Le mute physique de l'ampli est géré par la V1
            // On envoie juste le statut au serveur + visage

            // Envoyer l'émotion au visage
            if (emotionQueue) {
              const char* em = irSleepMode ? "veille" : "neutre";
              xQueueSend(emotionQueue, em, 0);
            }
          }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);  // Poll 20x/s
      }
    },
    "IR",     // Nom de la tâche
    4096,     // Stack : IRremoteESP8266 a besoin de ~2 Ko
    NULL,
    1,        // Priorité minimale
    NULL,
    0         // Core 0 : même que networkTask
  );


  Serial.println("[ROBOT] Toutes les tâches lancées.");
}

// Loop vide — toute la logique est dans les tâches FreeRTOS
void loop() { vTaskDelete(NULL); }
