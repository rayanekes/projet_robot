// face_renderer.cpp - Visages vectoriels LVGL pour Robot IA v3.0
// Remplace le système BMP/SD par du dessin vectoriel temps-réel.
// Dépendances : LVGL v8, TFT_eSPI, esp_heap_caps
#include "face_renderer.h"

// ============================================================
//  LVGL Driver Callbacks (même pattern que gui_spotify.cpp)
// ============================================================
static TFT_eSPI* _g_tft = nullptr;

static void _face_disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    if (_g_tft) {
        uint32_t w = area->x2 - area->x1 + 1;
        uint32_t h = area->y2 - area->y1 + 1;
        _g_tft->startWrite();
        _g_tft->setAddrWindow(area->x1, area->y1, w, h);
        _g_tft->pushColors((uint16_t*)&color_p->full, w * h, true);
        _g_tft->endWrite();
    }
    lv_disp_flush_ready(drv);
}

// ============================================================
//  Constructeur / Destructeur
// ============================================================
FaceRenderer::FaceRenderer() {}

FaceRenderer::~FaceRenderer() {
    if (_initialized) deinit();
}

// ============================================================
//  init() — Initialise LVGL + drivers + objets de base
// ============================================================
void FaceRenderer::init(TFT_eSPI* tft) {
    if (_initialized) return;
    _tft = tft;
    _g_tft = tft;

    // Buffer réduit à 10 lignes avec malloc standard (plus de contrainte DMA)
    _buf1 = (lv_color_t*)malloc(SCR_W * 10 * sizeof(lv_color_t));
    _draw_buf = (lv_disp_draw_buf_t*)malloc(sizeof(lv_disp_draw_buf_t));
    _disp_drv = (lv_disp_drv_t*)malloc(sizeof(lv_disp_drv_t));

    if (!_buf1 || !_draw_buf || !_disp_drv) {
        Serial.println("[FACE] ERREUR: Allocation LVGL échouée");
        return;
    }

    lv_disp_draw_buf_init(_draw_buf, _buf1, NULL, SCR_W * 10);
    lv_disp_drv_init(_disp_drv);
    _disp_drv->hor_res  = SCR_W;
    _disp_drv->ver_res  = SCR_H;
    _disp_drv->flush_cb = _face_disp_flush;
    _disp_drv->draw_buf = _draw_buf;
    _disp = lv_disp_drv_register(_disp_drv);

    _screen = lv_disp_get_scr_act(_disp);
    lv_obj_set_style_bg_color(_screen, _colorBg(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    _buildBaseObjects();
    _applyEmotion(FaceEmotion::NEUTRE, false);

    _initialized = true;
    Serial.println("[FACE] FaceRenderer initialisé (vectoriel LVGL)");
}

// ============================================================
//  deinit()
// ============================================================
void FaceRenderer::deinit() {
    if (!_initialized) return;
    _destroyDecoObjects();
    
    if (_disp) {
        lv_obj_clean(lv_disp_get_scr_act(_disp));
        lv_disp_remove(_disp);
        _disp = nullptr;
    }
    
    if (_buf1)     { free(_buf1);     _buf1 = nullptr; }
    if (_draw_buf) { free(_draw_buf); _draw_buf = nullptr; }
    if (_disp_drv) { free(_disp_drv); _disp_drv = nullptr; }
    _g_tft = nullptr;
    _initialized = false;
}

// ============================================================
//  _buildBaseObjects() — Crée tous les objets LVGL permanents
// ============================================================
void FaceRenderer::_buildBaseObjects() {

    // --- FOND avec légère vignette ---
    _bg = lv_obj_create(_screen);
    lv_obj_set_size(_bg, SCR_W, SCR_H);
    lv_obj_set_pos(_bg, 0, 0);
    lv_obj_set_style_bg_color(_bg, _colorBg(), 0);
    lv_obj_set_style_bg_opa(_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bg, 0, 0);
    lv_obj_clear_flag(_bg, LV_OBJ_FLAG_SCROLLABLE);

    // --- JOUES ROSES (cercles semi-transparents) ---
    _cheek_left = lv_obj_create(_screen);
    lv_obj_set_size(_cheek_left, 38, 28);
    lv_obj_set_pos(_cheek_left, 38, 122);
    lv_obj_set_style_bg_color(_cheek_left, lv_color_hex(0xFF6080), 0);
    lv_obj_set_style_bg_opa(_cheek_left, 90, 0);
    lv_obj_set_style_radius(_cheek_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_cheek_left, 0, 0);

    _cheek_right = lv_obj_create(_screen);
    lv_obj_set_size(_cheek_right, 38, 28);
    lv_obj_set_pos(_cheek_right, 244, 122);
    lv_obj_set_style_bg_color(_cheek_right, lv_color_hex(0xFF6080), 0);
    lv_obj_set_style_bg_opa(_cheek_right, 90, 0);
    lv_obj_set_style_radius(_cheek_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_cheek_right, 0, 0);

    // --- ŒIL GAUCHE (carré arrondi, couleur dynamique) ---
    _eye_left = lv_obj_create(_screen);
    lv_obj_set_size(_eye_left, EYE_W, EYE_H);
    lv_obj_set_pos(_eye_left, EYE_LX - EYE_W/2, EYE_Y - EYE_H/2);
    lv_obj_set_style_bg_color(_eye_left, _colorNeutre(), 0);
    lv_obj_set_style_bg_opa(_eye_left, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_eye_left, 16, 0);
    lv_obj_set_style_border_width(_eye_left, 0, 0);
    lv_obj_clear_flag(_eye_left, LV_OBJ_FLAG_SCROLLABLE);

    // --- PUPILLE GAUCHE (petit cercle sombre, donne du relief) ---
    _pupil_left = lv_obj_create(_eye_left);
    lv_obj_set_size(_pupil_left, 22, 22);
    lv_obj_center(_pupil_left);
    lv_obj_set_style_bg_color(_pupil_left, lv_color_hex(0x001A1A), 0);
    lv_obj_set_style_bg_opa(_pupil_left, 200, 0);
    lv_obj_set_style_radius(_pupil_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_pupil_left, 0, 0);

    // --- REFLET GAUCHE (petit point blanc en haut-droite) ---
    _shine_left = lv_obj_create(_eye_left);
    lv_obj_set_size(_shine_left, 8, 8);
    lv_obj_set_pos(_shine_left, EYE_W - 20, 12);
    lv_obj_set_style_bg_color(_shine_left, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_shine_left, 220, 0);
    lv_obj_set_style_radius(_shine_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_shine_left, 0, 0);

    // --- ŒIL DROIT ---
    _eye_right = lv_obj_create(_screen);
    lv_obj_set_size(_eye_right, EYE_W, EYE_H);
    lv_obj_set_pos(_eye_right, EYE_RX - EYE_W/2, EYE_Y - EYE_H/2);
    lv_obj_set_style_bg_color(_eye_right, _colorNeutre(), 0);
    lv_obj_set_style_bg_opa(_eye_right, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_eye_right, 16, 0);
    lv_obj_set_style_border_width(_eye_right, 0, 0);
    lv_obj_clear_flag(_eye_right, LV_OBJ_FLAG_SCROLLABLE);

    // --- PUPILLE DROITE ---
    _pupil_right = lv_obj_create(_eye_right);
    lv_obj_set_size(_pupil_right, 22, 22);
    lv_obj_center(_pupil_right);
    lv_obj_set_style_bg_color(_pupil_right, lv_color_hex(0x001A1A), 0);
    lv_obj_set_style_bg_opa(_pupil_right, 200, 0);
    lv_obj_set_style_radius(_pupil_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_pupil_right, 0, 0);

    // --- REFLET DROIT ---
    _shine_right = lv_obj_create(_eye_right);
    lv_obj_set_size(_shine_right, 8, 8);
    lv_obj_set_pos(_shine_right, EYE_W - 20, 12);
    lv_obj_set_style_bg_color(_shine_right, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_shine_right, 220, 0);
    lv_obj_set_style_radius(_shine_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_shine_right, 0, 0);

    // --- BOUCHE EXTÉRIEURE (contour) ---
    _mouth_outer = lv_obj_create(_screen);
    lv_obj_set_size(_mouth_outer, 90, 28);
    lv_obj_set_pos(_mouth_outer, SCR_W/2 - 45, MOUTH_Y - 14);
    lv_obj_set_style_bg_color(_mouth_outer, _colorNeutre(), 0);
    lv_obj_set_style_bg_opa(_mouth_outer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_mouth_outer, 14, 0);
    lv_obj_set_style_border_width(_mouth_outer, 0, 0);
    lv_obj_clear_flag(_mouth_outer, LV_OBJ_FLAG_SCROLLABLE);

    // --- BOUCHE INTÉRIEURE (rouge, visible quand bouche ouverte) ---
    _mouth_inner = lv_obj_create(_mouth_outer);
    lv_obj_set_size(_mouth_inner, 70, 0);  // Hauteur 0 = fermée
    lv_obj_align(_mouth_inner, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_mouth_inner, lv_color_hex(0xFF2020), 0);
    lv_obj_set_style_bg_opa(_mouth_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_mouth_inner, 8, 0);
    lv_obj_set_style_border_width(_mouth_inner, 0, 0);
}

// ============================================================
//  _destroyDecoObjects() — Supprime les objets décoratifs
//  (ceux qui sont spécifiques à une émotion)
// ============================================================
void FaceRenderer::_destroyDecoObjects() {
    if (_deco_1)     { lv_obj_del(_deco_1);     _deco_1 = nullptr; }
    if (_deco_2)     { lv_obj_del(_deco_2);     _deco_2 = nullptr; }
    if (_scan_ring)  { lv_obj_del(_scan_ring);  _scan_ring = nullptr; }
}

// ============================================================
//  setEmotion() — Thread-safe via lv_async_call
// ============================================================
struct EmotionCallArg { FaceRenderer* self; FaceEmotion emotion; };

static void _emotionCallback(void* arg) {
    EmotionCallArg* a = static_cast<EmotionCallArg*>(arg);
    a->self->_applyEmotion(a->emotion, true);
    free(arg);
}

void FaceRenderer::setEmotion(FaceEmotion emotion) {
    if (emotion == _emotion) return;
    _prevEmotion = _emotion;
    _emotion = emotion;

    // Appel thread-safe depuis n'importe quelle tâche FreeRTOS
    EmotionCallArg* arg = (EmotionCallArg*)malloc(sizeof(EmotionCallArg));
    if (arg) {
        arg->self    = this;
        arg->emotion = emotion;
        lv_async_call(_emotionCallback, arg);
    }
}

void FaceRenderer::setEmotionFromString(const String& name) {
    if      (name == "neutre"    || name == "idle")     setEmotion(FaceEmotion::NEUTRE);
    else if (name == "ecoute"    || name == "listening") setEmotion(FaceEmotion::ECOUTE);
    else if (name == "reflexion" || name == "thinking")  setEmotion(FaceEmotion::REFLEXION);
    else if (name == "parle"     || name == "speaking")  setEmotion(FaceEmotion::PARLE);
    else if (name == "erreur"    || name == "error")     setEmotion(FaceEmotion::ERREUR);
}

// ============================================================
//  setAudioRMS() — Appelé depuis speakerTask
// ============================================================
void FaceRenderer::setAudioRMS(float rms) {
    _audioRMS = rms;
}

// ============================================================
//  tick() — Boucle principale (appeler toutes les 5-10ms)
// ============================================================
void FaceRenderer::tick(uint32_t delta_ms) {
    if (!_initialized) return;
    uint32_t now = millis();

    _elapsed += delta_ms;

    // Clignement automatique (sauf ERREUR et PARLE)
    if (_emotion != FaceEmotion::ERREUR && _emotion != FaceEmotion::PARLE) {
        _updateBlink(now);
    }

    // Animation bouche/audio en mode PARLE
    if (_emotion == FaceEmotion::PARLE) {
        _updateAudioMouth(delta_ms);
    }

    // Animation "?" en REFLEXION
    if (_emotion == FaceEmotion::REFLEXION) {
        _updateReflexionAnim(delta_ms);
    }

    // Animation anneau en ECOUTE
    if (_emotion == FaceEmotion::ECOUTE && _scan_ring) {
        _updateScanRing(delta_ms);
    }
}

// ============================================================
//  _applyEmotion() — Dispatch
// ============================================================
void FaceRenderer::_applyEmotion(FaceEmotion emotion, bool animated) {
    _destroyDecoObjects();
    _blinking = false;

    // Remettre les joues visibles par défaut
    lv_obj_set_style_bg_opa(_cheek_left,  90, 0);
    lv_obj_set_style_bg_opa(_cheek_right, 90, 0);

    switch (emotion) {
        case FaceEmotion::NEUTRE:    _applyNeutre(animated);    break;
        case FaceEmotion::ECOUTE:    _applyEcoute(animated);    break;
        case FaceEmotion::REFLEXION: _applyReflexion(animated); break;
        case FaceEmotion::PARLE:     _applyParle(animated);     break;
        case FaceEmotion::ERREUR:    _applyErreur(animated);    break;
    }
}

// ============================================================
//  _applyNeutre() — Cyan, yeux carrés, bouche ligne neutre
// ============================================================
void FaceRenderer::_applyNeutre(bool animated) {
    lv_color_t col = _colorNeutre();

    // Yeux : taille normale, coins arrondis
    _setEyeOpenness(1.0f, 1.0f, animated);
    lv_obj_set_style_bg_color(_eye_left,    col, 0);
    lv_obj_set_style_bg_color(_eye_right,   col, 0);
    lv_obj_set_style_radius(_eye_left,  16, 0);
    lv_obj_set_style_radius(_eye_right, 16, 0);

    // Bouche : ligne horizontale (hauteur réduite, largeur max)
    lv_obj_set_size(_mouth_outer, 100, 14);
    lv_obj_set_pos(_mouth_outer, SCR_W/2 - 50, MOUTH_Y - 7);
    lv_obj_set_style_bg_color(_mouth_outer, col, 0);
    lv_obj_set_style_radius(_mouth_outer, 7, 0);
    lv_obj_set_size(_mouth_inner, 80, 0);
    lv_obj_set_style_bg_opa(_mouth_inner, LV_OPA_TRANSP, 0);
}

// ============================================================
//  _applyEcoute() — Vert, yeux agrandis, anneau pulsant
// ============================================================
void FaceRenderer::_applyEcoute(bool animated) {
    lv_color_t col = _colorEcoute();

    // Yeux légèrement plus grands (+15%)
    _setEyeOpenness(1.15f, 1.15f, animated);
    lv_obj_set_style_bg_color(_eye_left,  col, 0);
    lv_obj_set_style_bg_color(_eye_right, col, 0);
    lv_obj_set_style_radius(_eye_left,  20, 0);
    lv_obj_set_style_radius(_eye_right, 20, 0);

    // Bouche entrouverte (attente)
    lv_obj_set_size(_mouth_outer, 80, 22);
    lv_obj_set_pos(_mouth_outer, SCR_W/2 - 40, MOUTH_Y - 11);
    lv_obj_set_style_bg_color(_mouth_outer, col, 0);
    lv_obj_set_style_radius(_mouth_outer, 11, 0);
    lv_obj_set_size(_mouth_inner, 60, 8);
    lv_obj_set_style_bg_opa(_mouth_inner, LV_OPA_COVER, 0);

    // Anneau de scan autour du visage (bordure du screen)
    _scan_ring = lv_obj_create(_screen);
    lv_obj_set_size(_scan_ring, SCR_W - 6, SCR_H - 6);
    lv_obj_set_pos(_scan_ring, 3, 3);
    lv_obj_set_style_bg_opa(_scan_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(_scan_ring, col, 0);
    lv_obj_set_style_border_width(_scan_ring, 3, 0);
    lv_obj_set_style_border_opa(_scan_ring, 180, 0);
    lv_obj_set_style_radius(_scan_ring, 12, 0);
    lv_obj_clear_flag(_scan_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(_scan_ring); // Derrière les yeux
}

// ============================================================
//  _applyReflexion() — Magenta, asymétrique, "?" flottant
// ============================================================
void FaceRenderer::_applyReflexion(bool animated) {
    lv_color_t col = _colorReflexion();

    // Œil gauche : grand (cercle)
    lv_obj_set_size(_eye_left, EYE_W + 10, EYE_H + 10);
    lv_obj_set_pos(_eye_left, EYE_LX - (EYE_W+10)/2, EYE_Y - (EYE_H+10)/2);
    lv_obj_set_style_bg_color(_eye_left,  col, 0);
    lv_obj_set_style_radius(_eye_left, LV_RADIUS_CIRCLE, 0);

    // Œil droit : plissé (ligne fine)
    lv_obj_set_size(_eye_right, EYE_W, 12);
    lv_obj_set_pos(_eye_right, EYE_RX - EYE_W/2, EYE_Y - 6);
    lv_obj_set_style_bg_color(_eye_right, col, 0);
    lv_obj_set_style_radius(_eye_right, 6, 0);
    lv_obj_set_style_bg_opa(_pupil_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_shine_right, LV_OPA_TRANSP, 0);

    // Bouche tordue (ligne légèrement inclinée = simulée par 2 segments)
    lv_obj_set_size(_mouth_outer, 100, 10);
    lv_obj_set_pos(_mouth_outer, SCR_W/2 - 50, MOUTH_Y - 5);
    lv_obj_set_style_bg_color(_mouth_outer, col, 0);
    lv_obj_set_style_radius(_mouth_outer, 5, 0);
    lv_obj_set_style_bg_opa(_mouth_inner, LV_OPA_TRANSP, 0);

    // "?" label animé (deco_1)
    _deco_1 = lv_label_create(_screen);
    lv_label_set_text(_deco_1, "?");
    lv_obj_set_style_text_color(_deco_1, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(_deco_1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(_deco_1, 260, 10);

    // Point "." flottant (deco_2)
    _deco_2 = lv_obj_create(_screen);
    lv_obj_set_size(_deco_2, 10, 10);
    lv_obj_set_pos(_deco_2, 30, 30);
    lv_obj_set_style_bg_color(_deco_2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_deco_2, 160, 0);
    lv_obj_set_style_radius(_deco_2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_deco_2, 0, 0);

    _questionPhase = 0;
}

// ============================================================
//  _applyParle() — Vert menthe, bouche animée audio
// ============================================================
void FaceRenderer::_applyParle(bool animated) {
    lv_color_t col = _colorParle();

    _setEyeOpenness(1.0f, 1.0f, animated);
    lv_obj_set_style_bg_color(_eye_left,  col, 0);
    lv_obj_set_style_bg_color(_eye_right, col, 0);
    lv_obj_set_style_radius(_eye_left,  16, 0);
    lv_obj_set_style_radius(_eye_right, 16, 0);

    // Bouche : conteneur qui va s'animer via setMouthOpen()
    lv_obj_set_size(_mouth_outer, 90, 34);
    lv_obj_set_pos(_mouth_outer, SCR_W/2 - 45, MOUTH_Y - 17);
    lv_obj_set_style_bg_color(_mouth_outer, col, 0);
    lv_obj_set_style_radius(_mouth_outer, 17, 0);

    // Intérieur rouge (visible quand la bouche s'ouvre)
    lv_obj_set_size(_mouth_inner, 70, 6);
    lv_obj_align(_mouth_inner, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(_mouth_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_mouth_inner, 10, 0);

    _smoothRMS = 0.0f;
}

// ============================================================
//  _applyErreur() — Rouge, yeux en X, bouche inversée
// ============================================================
void FaceRenderer::_applyErreur(bool animated) {
    lv_color_t col = _colorErreur();

    // Cacher les yeux normaux
    lv_obj_set_style_bg_opa(_eye_left,  LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_eye_right, LV_OPA_TRANSP, 0);

    // Pas de joues
    lv_obj_set_style_bg_opa(_cheek_left,  LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_cheek_right, LV_OPA_TRANSP, 0);

    // "X" gauche (deux barres croisées, simulées par 2 rectangles pivotés)
    // LVGL v8 ne supporte pas la rotation de rects → on utilise 2 labels "✕"
    _deco_1 = lv_label_create(_screen);
    lv_label_set_text(_deco_1, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(_deco_1, col, 0);
    lv_obj_set_style_text_font(_deco_1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(_deco_1, EYE_LX - 30, EYE_Y - 30);

    _deco_2 = lv_label_create(_screen);
    lv_label_set_text(_deco_2, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(_deco_2, col, 0);
    lv_obj_set_style_text_font(_deco_2, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(_deco_2, EYE_RX - 30, EYE_Y - 30);

    // Bouche inversée (arc vers le bas = triste/erreur)
    lv_obj_set_size(_mouth_outer, 80, 20);
    lv_obj_set_pos(_mouth_outer, SCR_W/2 - 40, MOUTH_Y);
    lv_obj_set_style_bg_color(_mouth_outer, col, 0);
    lv_obj_set_style_radius(_mouth_outer, 10, 0);

    // Simuler la courbure vers le bas : affiner le haut du rectangle
    // avec une demi-ellipse invisible en haut
    lv_obj_set_style_bg_opa(_mouth_inner, LV_OPA_TRANSP, 0);
}

// ============================================================
//  _setEyeOpenness() — Redimensionne les yeux en hauteur
//  scale : 0.0 = fermé, 1.0 = normal, 1.15 = agrandi
// ============================================================
void FaceRenderer::_setEyeOpenness(float left, float right, bool animated) {
    // Œil gauche
    int16_t lh = max((int16_t)4, (int16_t)(EYE_H * left));
    int16_t ly = EYE_Y - lh / 2;
    lv_obj_set_size(_eye_left, EYE_W, lh);
    lv_obj_set_pos(_eye_left, EYE_LX - EYE_W/2, ly);
    lv_obj_set_style_bg_opa(_eye_left, left > 0.05f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_pupil_left, left > 0.3f ? 200 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_shine_left, left > 0.3f ? 220 : LV_OPA_TRANSP, 0);

    // Œil droit
    int16_t rh = max((int16_t)4, (int16_t)(EYE_H * right));
    int16_t ry = EYE_Y - rh / 2;
    lv_obj_set_size(_eye_right, EYE_W, rh);
    lv_obj_set_pos(_eye_right, EYE_RX - EYE_W/2, ry);
    lv_obj_set_style_bg_opa(_eye_right, right > 0.05f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_pupil_right, right > 0.3f ? 200 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_shine_right, right > 0.3f ? 220 : LV_OPA_TRANSP, 0);
}

// ============================================================
//  _setMouthOpen() — Anime l'ouverture de la bouche
//  openness : 0.0 = fermée, 1.0 = grande ouverte
// ============================================================
void FaceRenderer::_setMouthOpen(float openness) {
    openness = max(0.0f, min(1.0f, openness));
    int16_t innerH = (int16_t)(openness * 24.0f);
    lv_obj_set_size(_mouth_inner, 70, innerH);
    lv_obj_align(_mouth_inner, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ============================================================
//  _updateBlink()
// ============================================================
void FaceRenderer::_updateBlink(uint32_t now) {
    if (!_blinking) {
        if (now - _lastBlink > (uint32_t)config.blink_interval_ms) {
            _blinking  = true;
            _blinkStart = now;
            _setEyeOpenness(0.05f, 0.05f, false); // Yeux presque fermés
        }
    } else {
        if (now - _blinkStart > (uint32_t)config.blink_duration_ms) {
            _blinking  = false;
            _lastBlink = now;
            // Rouvrir selon l'émotion courante
            float scale = (_emotion == FaceEmotion::ECOUTE) ? 1.15f : 1.0f;
            _setEyeOpenness(scale, scale, false);
        }
    }
}

// ============================================================
//  _updateAudioMouth() — Lisse le RMS et anime la bouche
// ============================================================
void FaceRenderer::_updateAudioMouth(uint32_t delta_ms) {
    // Lissage exponentiel
    float alpha = 1.0f - expf(-(float)delta_ms / (float)config.mouth_smooth_ms);
    _smoothRMS  = _smoothRMS + alpha * (_audioRMS - _smoothRMS);

    float openness = 0.0f;
    if (_smoothRMS > config.rms_min_open) {
        openness = (_smoothRMS - config.rms_min_open) /
                   (config.rms_max_open - config.rms_min_open);
        openness = min(1.0f, openness * config.rms_scale);
    }

    // Clignement pendant PARLE (moins fréquent)
    uint32_t now = millis();
    if (!_blinking) {
        if (now - _lastBlink > (uint32_t)(config.blink_interval_ms * 2)) {
            _blinking   = true;
            _blinkStart = now;
            _setEyeOpenness(0.05f, 0.05f, false);
        }
    } else {
        if (now - _blinkStart > (uint32_t)config.blink_duration_ms) {
            _blinking  = false;
            _lastBlink = now;
            _setEyeOpenness(1.0f, 1.0f, false);
        }
    }

    _setMouthOpen(openness);
}

// ============================================================
//  _updateReflexionAnim() — "?" qui monte/descend + point clignotant
// ============================================================
void FaceRenderer::_updateReflexionAnim(uint32_t delta_ms) {
    _questionPhase += delta_ms;

    if (_deco_1) {
        // Oscillation verticale du "?" (±8px, période 800ms)
        float t = sinf((float)_questionPhase / 800.0f * 2.0f * 3.14159f);
        int16_t yOff = (int16_t)(t * 8.0f);
        lv_obj_set_pos(_deco_1, 260, 10 + yOff);
    }

    if (_deco_2) {
        // Clignotement du point (toutes les 600ms)
        lv_opa_t opa = ((_questionPhase / 600) % 2 == 0) ? 180 : 40;
        lv_obj_set_style_bg_opa(_deco_2, opa, 0);
    }
}

// ============================================================
//  _updateScanRing() — Pulsation de l'anneau ECOUTE
// ============================================================
void FaceRenderer::_updateScanRing(uint32_t delta_ms) {
    if (!_scan_ring) return;
    // Pulsation opacité (60-200, période 1.2s)
    float t = sinf((float)_elapsed / 1200.0f * 2.0f * 3.14159f);
    lv_opa_t opa = (lv_opa_t)(130 + (int)(t * 70.0f));
    lv_obj_set_style_border_opa(_scan_ring, opa, 0);
}
