#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>

#define SD_CS 5

class TFT_Display {
public:
    TFT_Display();
    void init();
    void displayEmotion(String emotion, int frame);
    TFT_eSPI* getTftPointer() { return &tft; }

private:
    TFT_eSPI tft;
    void drawBmp(const char *filename, int16_t x, int16_t y);
};

#endif // TFT_DISPLAY_H
