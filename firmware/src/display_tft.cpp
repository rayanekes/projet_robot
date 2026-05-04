#include "display_tft.h"

TFT_Display::TFT_Display() : tft(TFT_eSPI()) {}

void TFT_Display::init() {
    // Reset matériel sur TFT_RST (GPIO 13 défini dans platformio.ini)
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);
    delay(150);
    digitalWrite(TFT_RST, HIGH);
    delay(150);

    // Sur Arduino Core v3 / ESP-IDF 5, TFT_eSPI avec USE_FSPI_PORT
    // initialise lui-même le bus SPI. Appeler SPI.begin() avant est contre-productif.
    tft.init();
    tft.setRotation(1);  // Paysage (320x240)
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Robot IA...");
}

// NOTE: drawBmp() et displayEmotion() supprimés (FIX-005)
// Le projet utilise désormais FaceRenderer (LVGL vectoriel) pour toutes les animations.
// Aucune lecture SD n'est nécessaire pour l'affichage.
