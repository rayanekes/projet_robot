#include "network_ws.h"
#include "audio_chunk.h"

// Variables externes FreeRTOS définies dans main.cpp
extern QueueHandle_t emotionQueue;
extern QueueHandle_t commandQueue;
extern QueueHandle_t audioTxQueue;

// Définition de l'instance statique
WebSocketsClient Network_WS::webSocket;

// FIX-001 : Compteur de reconnexions pour limiter les logs répétitifs
static uint32_t _reconnect_count = 0;
static uint32_t _last_reconnect_log_ms = 0;

void Network_WS::initWiFi(const char* ssid, const char* password) {
    Serial.print("[WiFi] Connexion à : ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
}

void Network_WS::initWebSocket(const char* server_ip, uint16_t server_port) {
    Serial.printf("[WS] Connexion WebSocket → %s:%d\n", server_ip, server_port);
    webSocket.begin(server_ip, server_port, "/");
    webSocket.onEvent(webSocketEvent);
    // FIX-001 : Délai de reconnexion initial de 3s (augmentera en cas d'échecs répétés)
    webSocket.setReconnectInterval(3000);
    // Désactiver le ping WebSocket (le serveur Python a aussi ping_interval=None)
    webSocket.enableHeartbeat(0, 0, 0);
    _reconnect_count = 0;
}

void Network_WS::loop() {
    // Vérification Wi-Fi (non-bloquante)
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    webSocket.loop();
}

bool Network_WS::isConnected() {
    return webSocket.isConnected();
}

void Network_WS::sendAudio(const uint8_t *payload, size_t length) {
    if (webSocket.isConnected()) {
        // sendBIN retourne false si le buffer interne est plein — on ignore sans crash
        webSocket.sendBIN(payload, length);
    }
}

void Network_WS::sendStatus(const char* status) {
    if (webSocket.isConnected()) {
        // Envoyer un JSON {"status":"..."} au serveur (ex: "listening")
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"status\":\"%s\"}", status);
        webSocket.sendTXT(buf);
    }
}

void Network_WS::sendDebug(const char* message) {
    // Moniteur Série sans fil — envoie {"log":"..."} au serveur Python.
    // Le serveur écrit ce message dans logs/esp32_remote.log UNIQUEMENT
    // (pas dans la console, pas dans server.log).
    //
    // Contraintes respectées :
    //   • Appel conditionnel à isConnected() → aucun envoi si déconnecté
    //   • Buffer statique (pas de malloc) → sûr depuis n'importe quelle tâche
    //   • NE PAS appeler depuis micTask/speakerTask (boucles critiques audio)
    if (!webSocket.isConnected()) return;

    // Buffer : {"log":"<128 chars max>"} = ~140 octets
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"log\":\"%s\"}", message);
    webSocket.sendTXT(buf);
}

void Network_WS::sendCmd(const char* cmd) {
    // Envoie une commande IR au serveur Python : {"cmd":"cmd:lang:fr"}
    if (!webSocket.isConnected()) return;
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}", cmd);
    webSocket.sendTXT(buf);
}

void Network_WS::handleJsonMessage(uint8_t * payload) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf("[WS] Erreur JSON: %s\n", error.c_str());
        return;
    }

    // --- Commandes application (depuis Python) ---
    if (doc.containsKey("command")) {
        const char* command = doc["command"];
        char cmdToSend[32];
        strncpy(cmdToSend, command, sizeof(cmdToSend) - 1);
        cmdToSend[sizeof(cmdToSend) - 1] = '\0';
        if (commandQueue != NULL) {
            xQueueSend(commandQueue, &cmdToSend, 0);
        }
    }

    // --- Émotions directes ---
    if (doc.containsKey("emotion")) {
        const char* emotion = doc["emotion"];
        char emotionToSend[32];
        strncpy(emotionToSend, emotion, sizeof(emotionToSend) - 1);
        emotionToSend[sizeof(emotionToSend) - 1] = '\0';
        if (emotionQueue != NULL) {
            xQueueSend(emotionQueue, &emotionToSend, 0);
        }
    }

    // --- Mapping status → émotion visuelle ---
    if (doc.containsKey("status")) {
        const char* status = doc["status"];
        char emotionToSend[32] = "";

        if (strcmp(status, "connected") == 0) {
            Serial.println("[WS] Serveur prêt (calibration)");
            return;
        } else if (strcmp(status, "thinking") == 0) {
            strncpy(emotionToSend, "reflexion", sizeof(emotionToSend));
        } else if (strcmp(status, "speaking") == 0) {
            strncpy(emotionToSend, "parle", sizeof(emotionToSend));
        } else if (strcmp(status, "idle") == 0) {
            strncpy(emotionToSend, "idle", sizeof(emotionToSend));
        } else if (strcmp(status, "error") == 0) {
            strncpy(emotionToSend, "erreur", sizeof(emotionToSend));
        }

        if (strlen(emotionToSend) > 0 && emotionQueue != NULL) {
            xQueueSend(emotionQueue, &emotionToSend, 0);
        }
    }

    // ── Acquittement IR : retour visuel sur le visage du robot ──────────────
    // Le serveur envoie {"ir_ack":"cmd:lang:fr"} pour confirmer l'exécution.
    if (doc.containsKey("ir_ack")) {
        const char* ack = doc["ir_ack"];
        Serial.printf("[IR] ACK reçu : %s\n", ack);

        // Relayer vers la commandQueue pour traitement local (vol, bright)
        char cmdToSend[64];
        strncpy(cmdToSend, ack, sizeof(cmdToSend) - 1);
        cmdToSend[sizeof(cmdToSend) - 1] = '\0';
        if (commandQueue != NULL) {
            xQueueSend(commandQueue, &cmdToSend, 0);
        }

        // Feedback visuel immédiat selon la commande confirmée
        if (emotionQueue != NULL) {
            char em[32] = "";
            if (strncmp(ack, "cmd:lang:", 9) == 0) {
                // Changement de langue → clignement "réflexion" rapide puis retour
                strncpy(em, "reflexion", sizeof(em));
            } else if (strcmp(ack, "cmd:reset") == 0) {
                strncpy(em, "neutre", sizeof(em));
            } else if (strcmp(ack, "cmd:stop") == 0) {
                strncpy(em, "idle", sizeof(em));
            }
            if (strlen(em) > 0) {
                xQueueSend(emotionQueue, &em, 0);
            }
        }
    }
}

void Network_WS::webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            // FIX-001 : Limiter les logs de reconnexion répétés
            // Afficher seulement toutes les 10s pour ne pas noyer la console
            _reconnect_count++;
            if (millis() - _last_reconnect_log_ms > 10000 || _reconnect_count <= 3) {
                Serial.printf("[WS] Déconnecté (tentative #%u)\n", _reconnect_count);
                _last_reconnect_log_ms = millis();
            }
            // Vider la queue audio TX pour ne pas jouer des données obsolètes
            if (audioTxQueue != NULL) {
                AudioChunk staleChunk;
                while (xQueueReceive(audioTxQueue, &staleChunk, 0) == pdPASS) {
                    if (staleChunk.data) free(staleChunk.data);
                }
            }
            // Afficher l'émotion erreur sur le visage
            if (emotionQueue != NULL) {
                char em[32] = "erreur";
                xQueueSend(emotionQueue, &em, 0);
            }
            // Augmenter progressivement le délai de reconnexion (max 30s)
            {
                uint32_t delay_ms = min((uint32_t)(3000U + (_reconnect_count * 2000U)), (uint32_t)30000U);
                webSocket.setReconnectInterval(delay_ms);
            }
            break;

        case WStype_CONNECTED:
            Serial.printf("[WS] Connecté au serveur ! (après %u tentative(s))\n", _reconnect_count);
            _reconnect_count = 0;
            // Réinitialiser l'intervalle de reconnexion après succès
            webSocket.setReconnectInterval(3000);
            break;

        case WStype_TEXT:
            // Message JSON du serveur (statut, émotion, commande)
            handleJsonMessage(payload);
            break;

        case WStype_BIN:
            // Audio TTS PCM brut envoyé par le serveur (à jouer sur le MAX98357A)
            if (audioTxQueue != NULL && length > 0) {
                uint8_t* audioData = (uint8_t*)malloc(length);
                if (audioData != NULL) {
                    memcpy(audioData, payload, length);
                    AudioChunk chunk = { audioData, length };
                    if (xQueueSend(audioTxQueue, &chunk, 0) != pdPASS) {
                        // Queue pleine → libérer la mémoire immédiatement
                        free(audioData);
                    }
                } else {
                    Serial.println("[WS] ERREUR: malloc audio échoué !");
                }
            }
            break;

        case WStype_ERROR:
            Serial.println("[WS] Erreur WebSocket détectée");
            break;

        default:
            break;
    }
}
