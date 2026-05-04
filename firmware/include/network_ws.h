#ifndef NETWORK_WS_H
#define NETWORK_WS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ═══ Moniteur Série sans fil (Remote Logger) ═══════════════════════
// Mettre à false pour désactiver tous les REMOTELOG en une ligne.
#define ENABLE_REMOTE_DEBUG true

// Macro sécurisée :
//   • Activée  : formate et envoie le log via WebSocket (JSON {"log":"..."})
//   • Désactivée : compilée à zéro (aucun overhead CPU ni mémoire)
// INTERDIT dans micTask / speakerTask (boucles critiques audio).
#if ENABLE_REMOTE_DEBUG
  #define REMOTELOG(fmt, ...) \
    do { \
      if (network && network->isConnected()) { \
        char _rlog_buf[128]; \
        snprintf(_rlog_buf, sizeof(_rlog_buf), fmt, ##__VA_ARGS__); \
        network->sendDebug(_rlog_buf); \
      } \
    } while(0)
#else
  #define REMOTELOG(fmt, ...) do {} while(0)
#endif
// ═════════════════════════════════════════════════════════════════════════════

class Network_WS {
public:
    void initWiFi(const char* ssid, const char* password);
    void initWebSocket(const char* server_ip, uint16_t server_port);

    // Maintenir la connexion active (appeler dans la tâche réseau)
    void loop();

    // Envoyer un buffer binaire (audio micro) au serveur
    void sendAudio(const uint8_t *payload, size_t length);

    // Envoyer un statut JSON au serveur (ex: {"status":"listening"})
    void sendStatus(const char* status);

    // Envoyer un log texte au serveur → logs/esp32_remote.log
    // NE PAS appeler depuis micTask/speakerTask (boucles audio critiques).
    void sendDebug(const char* message);
    void sendCmd(const char* cmd);      // Commande IR → serveur Python

    // État de la connexion WebSocket
    bool isConnected();

    // Callback statique pour la bibliothèque WebSockets
    static void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);

private:
    static WebSocketsClient webSocket;
    static void handleJsonMessage(uint8_t * payload);
};

#endif // NETWORK_WS_H
