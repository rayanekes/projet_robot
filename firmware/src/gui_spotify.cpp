#include "gui_spotify.h"
#include <esp_heap_caps.h>

GuiSpotify::GuiSpotify() 
    : _tft(nullptr), title_label(nullptr), artist_label(nullptr),
      progress_bar(nullptr), play_btn(nullptr), play_btn_label(nullptr),
      time_label(nullptr), draw_buf(nullptr), buf1(nullptr),
      disp_drv(nullptr), disp(nullptr), isPlayBtnClicked(false) {}

// Le pointeur TFT est stocké globalement *seulement pour le callback LVGL*
static TFT_eSPI* global_tft_ptr = nullptr;
static lv_indev_drv_t indev_drv; // Driver pour le tactile
static lv_indev_t* indev_touchpad;

// Callback de dessin matériel pour LVGL v8
static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (global_tft_ptr) {
        uint32_t w = (area->x2 - area->x1 + 1);
        uint32_t h = (area->y2 - area->y1 + 1);

        global_tft_ptr->startWrite();
        global_tft_ptr->setAddrWindow(area->x1, area->y1, w, h);
        global_tft_ptr->pushColors((uint16_t *)&color_p->full, w * h, true);
        global_tft_ptr->endWrite();
    }
    lv_disp_flush_ready(disp_drv);
}

// Callback de lecture tactile pour LVGL v8
static void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    uint16_t touchX = 0, touchY = 0;
    bool touched = false;

    if (global_tft_ptr) {
        touched = global_tft_ptr->getTouch(&touchX, &touchY);
        if (touched) {
            Serial.printf("Tactile capté : X=%d, Y=%d\n", touchX, touchY);
        }
    }

    if (!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_PR;
        // Inversion ou mapping manuel si nécessaire
        data->point.x = touchX;
        data->point.y = touchY;
    }
}

void GuiSpotify::init(TFT_eSPI* tft) {
    if (disp != nullptr) return; // Déjà initialisé

    _tft = tft;
    global_tft_ptr = tft;

    uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
    _tft->setTouch(calData);

    // Buffer réduit à 10 lignes avec malloc standard (plus de contrainte DMA)
    buf1 = (lv_color_t*)malloc(screenWidth * 10 * sizeof(lv_color_t));
    draw_buf = (lv_disp_draw_buf_t*)malloc(sizeof(lv_disp_draw_buf_t));
    disp_drv = (lv_disp_drv_t*)malloc(sizeof(lv_disp_drv_t));

    if (!buf1 || !draw_buf || !disp_drv) {
        Serial.println("[GUI] Erreur allocation mémoire LVGL");
        return;
    }

    lv_disp_draw_buf_init(draw_buf, buf1, NULL, screenWidth * 10);

    lv_disp_drv_init(disp_drv);
    disp_drv->hor_res = screenWidth;
    disp_drv->ver_res = screenHeight;
    disp_drv->flush_cb = my_disp_flush;
    disp_drv->draw_buf = draw_buf;
    disp = lv_disp_drv_register(disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv);
}

static void play_event_cb(lv_event_t * e) {
    GuiSpotify * ui = (GuiSpotify *)lv_event_get_user_data(e);
    if (ui) {
        ui->isPlayBtnClicked = true;
    }
}

static void quit_event_cb(lv_event_t * e) {
    GuiSpotify * ui = (GuiSpotify *)lv_event_get_user_data(e);
    if (ui) { ui->isQuitBtnClicked = true; }
}

void GuiSpotify::buildInterface() {
    if (!disp) return;
    lv_obj_t * scr = lv_disp_get_scr_act(disp);
    if (!scr) return;
    
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121212), 0); // Couleur Spotify Dark
    
    // Bouton de sortie (petit 'X' en haut à droite)
    quit_btn = lv_btn_create(scr);
    lv_obj_set_size(quit_btn, 40, 40);
    lv_obj_set_style_bg_color(quit_btn, lv_color_hex(0x333333), 0);
    lv_obj_align(quit_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(quit_btn, quit_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t * quit_label = lv_label_create(quit_btn);
    lv_label_set_text(quit_label, LV_SYMBOL_CLOSE);
    lv_obj_center(quit_label);

    title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "Chargement...");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 40);

    artist_label = lv_label_create(scr);
    lv_label_set_text(artist_label, "Artiste Inconnu");
    lv_obj_set_style_text_color(artist_label, lv_color_hex(0xB3B3B3), 0);
    lv_obj_set_style_text_font(artist_label, &lv_font_montserrat_14, 0);
    lv_obj_align(artist_label, LV_ALIGN_TOP_MID, 0, 70);

    // Barre de progression
    progress_bar = lv_bar_create(scr);
    lv_obj_set_size(progress_bar, 260, 6);
    lv_obj_align(progress_bar, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x1DB954), LV_PART_INDICATOR); // Vert Spotify
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x535353), LV_PART_MAIN);

    // Temps
    time_label = lv_label_create(scr);
    lv_label_set_text(time_label, "0:00 / 0:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xB3B3B3), 0);
    lv_obj_align_to(time_label, progress_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // Bouton Play/Pause (On utilise un simple label texte pour simuler une icône ici)
    play_btn = lv_btn_create(scr);
    lv_obj_set_size(play_btn, 60, 60);
    lv_obj_set_style_radius(play_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_white(), 0);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(play_btn, play_event_cb, LV_EVENT_CLICKED, this);

    play_btn_label = lv_label_create(play_btn);
    lv_label_set_text(play_btn_label, "||"); // "||" pour pause, ">" pour play
    lv_obj_set_style_text_color(play_btn_label, lv_color_black(), 0);
    lv_obj_center(play_btn_label);
}

void GuiSpotify::updateTitle(const char* title, const char* artist) {
    if(title_label) lv_label_set_text(title_label, title);
    if(artist_label) lv_label_set_text(artist_label, artist);
}

void GuiSpotify::updateProgress(uint32_t current_time, uint32_t total_time) {
    if(total_time > 0 && progress_bar) {
        int pct = (current_time * 100) / total_time;
        lv_bar_set_value(progress_bar, pct, LV_ANIM_OFF);

        char time_str[32];
        sprintf(time_str, "%lu:%02lu / %lu:%02lu",
            current_time/60, current_time%60,
            total_time/60, total_time%60);
        lv_label_set_text(time_label, time_str);
    }
}

void GuiSpotify::togglePlayPauseIcon(bool isPlaying) {
    if(play_btn_label) {
        lv_label_set_text(play_btn_label, isPlaying ? "||" : ">");
    }
}

void GuiSpotify::deinit() {
    if (!disp) return;

    // Supprime proprement l'écran et ses enfants
    lv_obj_clean(lv_disp_get_scr_act(disp));

    lv_indev_delete(indev_touchpad);
    indev_touchpad = nullptr;

    lv_disp_remove(disp);
    disp = nullptr;

    if (buf1) { free(buf1); buf1 = nullptr; }
    if (draw_buf) { free(draw_buf); draw_buf = nullptr; }
    if (disp_drv) { free(disp_drv); disp_drv = nullptr; }

    global_tft_ptr = nullptr;
}
