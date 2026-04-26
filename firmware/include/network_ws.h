#ifndef NETWORK_WS_H
#define NETWORK_WS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

class Network_WS {
public:
    void initWiFi(const char* ssid, const char* password);
    void initWebSocket(const char* server_ip, uint16_t server_port);

    // Doit être appelé continuellement dans la boucle (ou la tâche FreeRTOS)
    void loop();

    // Permet d'envoyer un buffer binaire (audio du micro) au serveur
    void sendAudio(const uint8_t *payload, size_t length);
    bool isConnected(); // Nouvelle méthode

    // Fonction de rappel interne pour la librairie WebSockets
    static void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);

private:
    static WebSocketsClient webSocket;
    static void handleJsonMessage(uint8_t * payload);
};

#endif // NETWORK_WS_H
