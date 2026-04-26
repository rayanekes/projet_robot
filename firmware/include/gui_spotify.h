#ifndef GUI_SPOTIFY_H
#define GUI_SPOTIFY_H

#include <lvgl.h>
#include <TFT_eSPI.h>

class GuiSpotify {
public:
    GuiSpotify(); // Constructeur explicite
    void init(TFT_eSPI* tft);
    void buildInterface();
    void updateProgress(uint32_t current_time, uint32_t total_time);
    void updateTitle(const char* title, const char* artist);
    void togglePlayPauseIcon(bool isPlaying);
    void deinit(); // Pour libérer la RAM lors du retour au mode IA

    bool isPlayBtnClicked = false;
    bool isQuitBtnClicked = false;

private:
    TFT_eSPI* _tft;
    static const uint16_t screenWidth  = 320;
    static const uint16_t screenHeight = 240;

    // Objets LVGL
    lv_obj_t * title_label;
    lv_obj_t * artist_label;
    lv_obj_t * progress_bar;
    lv_obj_t * play_btn;
    lv_obj_t * play_btn_label;
    lv_obj_t * quit_btn; // Nouveau bouton
    lv_obj_t * time_label;

    // Buffers LVGL (Alloués dynamiquement)
    lv_disp_draw_buf_t* draw_buf;
    lv_color_t* buf1;
    lv_disp_drv_t* disp_drv;
    lv_disp_t* disp;
};

#endif
