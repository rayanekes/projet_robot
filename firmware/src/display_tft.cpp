#include "display_tft.h"

TFT_Display::TFT_Display() : tft(TFT_eSPI()) {}

void TFT_Display::init() {
  // Reset matériel sur Pin 4
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  delay(100);
  digitalWrite(4, HIGH);
  delay(100);

  // Initialisation du TFT
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Robot IA en ligne !");
}

void TFT_Display::displayEmotion(String emotion, int frame) {
  String bmpPath = "/" + emotion + "_" + String(frame) + ".bmp";
  drawBmp(bmpPath.c_str(), 0, 0); // Plus de fillScreen pour éviter le flash
}

void TFT_Display::drawBmp(const char *filename, int16_t x, int16_t y) {
  if ((x >= tft.width()) || (y >= tft.height())) return;

  File bmpFS = SD.open(filename, "r");
  if (!bmpFS) {
    Serial.printf("Fichier introuvable: %s\n", filename);
    return;
  }

  if (bmpFS.read() == 'B' && bmpFS.read() == 'M') {
    bmpFS.seek(10);
    uint32_t seekOffset = bmpFS.read() | (bmpFS.read() << 8) | (bmpFS.read() << 16) | (bmpFS.read() << 24);
    bmpFS.seek(18);
    uint32_t w = bmpFS.read() | (bmpFS.read() << 8) | (bmpFS.read() << 16) | (bmpFS.read() << 24);
    uint32_t h = bmpFS.read() | (bmpFS.read() << 8) | (bmpFS.read() << 16) | (bmpFS.read() << 24);
    bmpFS.seek(28);
    if (bmpFS.read() == 24) { // 24-bit BMP
      bmpFS.seek(seekOffset);
      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t* lineBuffer = (uint8_t*)malloc(w * 3 + padding);
      uint16_t* colorBuffer = (uint16_t*)malloc(w * 2);

      if (lineBuffer && colorBuffer) {
        tft.startWrite(); // Début transaction SPI groupée
        for (uint16_t row = 0; row < h; row++) {
          bmpFS.read(lineBuffer, w * 3 + padding);
          for (uint16_t col = 0; col < w; col++) {
            uint8_t b = lineBuffer[col * 3];
            uint8_t g = lineBuffer[col * 3 + 1];
            uint8_t r = lineBuffer[col * 3 + 2];
            colorBuffer[col] = (r & 0xF8) << 8 | (g & 0xFC) << 3 | (b >> 3);
          }
          tft.setAddrWindow(x, y + h - 1 - row, w, 1);
          tft.pushColors(colorBuffer, w, true);
        }
        tft.endWrite(); // Fin transaction
      }
      free(lineBuffer); free(colorBuffer);
    }
  }
  bmpFS.close();
}
