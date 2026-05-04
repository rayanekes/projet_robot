#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
// NOTE: Architecture ESP32-S3 simplifiée — tactile XPT2046 et carte SD supprimés
// L'affichage utilise LVGL vectoriel (FaceRenderer) via TFT_eSPI

class TFT_Display {
public:
    TFT_Display();
    void init();
    TFT_eSPI* getTftPointer() { return &tft; }

private:
    TFT_eSPI tft;
    // drawBmp() supprimé : code mort depuis migration vers LVGL vectoriel (FIX-005)
    // displayEmotion() supprimé : idem, les émotions sont gérées par FaceRenderer
};

#endif // TFT_DISPLAY_H
