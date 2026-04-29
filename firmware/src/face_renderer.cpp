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

// --- Helper local pour snap ---
static void _snapElement(ElementState& el, float x, float y, float w, float h, float r, float opa, uint32_t color) {
    el.x.snap(x); el.y.snap(y); el.w.snap(w); el.h.snap(h);
    el.radius.snap(r); el.opa.snap(opa); el.color.snap(color);
}
static void _setElement(ElementState& el, float x, float y, float w, float h, float r, float opa, uint32_t color) {
    el.x.set(x); el.y.set(y); el.w.set(w); el.h.set(h);
    el.radius.set(r); el.opa.set(opa); el.color.set(color);
}
static void _applyElementToObj(lv_obj_t* obj, const ElementState& el, float breathOffY = 0.0f) {
    if (!obj) return;
    lv_obj_set_size(obj, (lv_coord_t)el.w.current, (lv_coord_t)el.h.current);
    lv_obj_set_pos(obj, (lv_coord_t)el.x.current, (lv_coord_t)(el.y.current + breathOffY));
    lv_obj_set_style_radius(obj, (lv_coord_t)el.radius.current, 0);
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)el.opa.current, 0);
    lv_obj_set_style_bg_color(obj, el.color.get(), 0);
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

    // Initialise l'état au neutre (snap)
    uint32_t cN = _colorNeutreHex();
    _snapElement(_state.eye_l, EYE_LX - EYE_W/2, EYE_Y - EYE_H/2, EYE_W, EYE_H, 16, 255, cN);
    _snapElement(_state.eye_r, EYE_RX - EYE_W/2, EYE_Y - EYE_H/2, EYE_W, EYE_H, 16, 255, cN);
    _snapElement(_state.pupil_l, EYE_LX - 11, EYE_Y - 11, 22, 22, 11, 200, 0x001A1A);
    _snapElement(_state.pupil_r, EYE_RX - 11, EYE_Y - 11, 22, 22, 11, 200, 0x001A1A);
    _snapElement(_state.shine_l, EYE_LX + EYE_W/2 - 20, EYE_Y - EYE_H/2 + 12, 8, 8, 4, 220, 0xFFFFFF);
    _snapElement(_state.shine_r, EYE_RX + EYE_W/2 - 20, EYE_Y - EYE_H/2 + 12, 8, 8, 4, 220, 0xFFFFFF);
    _snapElement(_state.mouth_outer, SCR_W/2 - 50, MOUTH_Y - 7, 100, 14, 7, 255, cN);
    _snapElement(_state.mouth_inner, SCR_W/2 - 40, MOUTH_Y - 7 + 14, 80, 0, 0, 0, 0xFF2020);
    _snapElement(_state.cheek_l, 38, 122, 38, 28, 14, 90, 0xFF6080);
    _snapElement(_state.cheek_r, 244, 122, 38, 28, 14, 90, 0xFF6080);

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
    _pupil_left = lv_obj_create(_screen);
    lv_obj_set_size(_pupil_left, 22, 22);
    lv_obj_set_pos(_pupil_left, EYE_LX - 11, EYE_Y - 11);
    lv_obj_set_style_bg_color(_pupil_left, lv_color_hex(0x001A1A), 0);
    lv_obj_set_style_bg_opa(_pupil_left, 200, 0);
    lv_obj_set_style_radius(_pupil_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_pupil_left, 0, 0);

    // --- REFLET GAUCHE (petit point blanc en haut-droite) ---
    _shine_left = lv_obj_create(_screen);
    lv_obj_set_size(_shine_left, 8, 8);
    lv_obj_set_pos(_shine_left, EYE_LX + EYE_W/2 - 20, EYE_Y - EYE_H/2 + 12);
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
    _pupil_right = lv_obj_create(_screen);
    lv_obj_set_size(_pupil_right, 22, 22);
    lv_obj_set_pos(_pupil_right, EYE_RX - 11, EYE_Y - 11);
    lv_obj_set_style_bg_color(_pupil_right, lv_color_hex(0x001A1A), 0);
    lv_obj_set_style_bg_opa(_pupil_right, 200, 0);
    lv_obj_set_style_radius(_pupil_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_pupil_right, 0, 0);

    // --- REFLET DROIT ---
    _shine_right = lv_obj_create(_screen);
    lv_obj_set_size(_shine_right, 8, 8);
    lv_obj_set_pos(_shine_right, EYE_RX + EYE_W/2 - 20, EYE_Y - EYE_H/2 + 12);
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
    _mouth_inner = lv_obj_create(_screen);
    lv_obj_set_size(_mouth_inner, 70, 0);  // Hauteur 0 = fermée
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

    // Mettre à jour la respiration (offset Y)
    _breathPhase += delta_ms;
    float breathOffY = sinf((float)_breathPhase / 1500.0f * 2.0f * 3.14159f) * 3.0f; // ±3px

    // Mouvement procédural des yeux (look)
    if (now - _lastLookTime > 2000 && random(100) < 5) {
        _lastLookTime = now;
        _lookX = (random(200) - 100) / 100.0f * 6.0f; // ±6px
        _lookY = (random(200) - 100) / 100.0f * 4.0f; // ±4px
    } else if (now - _lastLookTime > 3000) {
        _lookX = 0; _lookY = 0; // Retour au centre
    }

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

    // Lissage des valeurs
    float alpha = 1.0f - expf(-(float)delta_ms / 60.0f);

    _state.eye_l.w.update(alpha); _state.eye_l.h.update(alpha); _state.eye_l.x.update(alpha); _state.eye_l.y.update(alpha); _state.eye_l.radius.update(alpha); _state.eye_l.opa.update(alpha); _state.eye_l.color.update(alpha);
    _state.eye_r.w.update(alpha); _state.eye_r.h.update(alpha); _state.eye_r.x.update(alpha); _state.eye_r.y.update(alpha); _state.eye_r.radius.update(alpha); _state.eye_r.opa.update(alpha); _state.eye_r.color.update(alpha);
    _state.pupil_l.w.update(alpha); _state.pupil_l.h.update(alpha); _state.pupil_l.x.update(alpha); _state.pupil_l.y.update(alpha); _state.pupil_l.radius.update(alpha); _state.pupil_l.opa.update(alpha); _state.pupil_l.color.update(alpha);
    _state.pupil_r.w.update(alpha); _state.pupil_r.h.update(alpha); _state.pupil_r.x.update(alpha); _state.pupil_r.y.update(alpha); _state.pupil_r.radius.update(alpha); _state.pupil_r.opa.update(alpha); _state.pupil_r.color.update(alpha);
    _state.shine_l.w.update(alpha); _state.shine_l.h.update(alpha); _state.shine_l.x.update(alpha); _state.shine_l.y.update(alpha); _state.shine_l.radius.update(alpha); _state.shine_l.opa.update(alpha); _state.shine_l.color.update(alpha);
    _state.shine_r.w.update(alpha); _state.shine_r.h.update(alpha); _state.shine_r.x.update(alpha); _state.shine_r.y.update(alpha); _state.shine_r.radius.update(alpha); _state.shine_r.opa.update(alpha); _state.shine_r.color.update(alpha);
    _state.mouth_outer.w.update(alpha); _state.mouth_outer.h.update(alpha); _state.mouth_outer.x.update(alpha); _state.mouth_outer.y.update(alpha); _state.mouth_outer.radius.update(alpha); _state.mouth_outer.opa.update(alpha); _state.mouth_outer.color.update(alpha);
    _state.mouth_inner.w.update(alpha); _state.mouth_inner.h.update(alpha); _state.mouth_inner.x.update(alpha); _state.mouth_inner.y.update(alpha); _state.mouth_inner.radius.update(alpha); _state.mouth_inner.opa.update(alpha); _state.mouth_inner.color.update(alpha);
    _state.cheek_l.w.update(alpha); _state.cheek_l.h.update(alpha); _state.cheek_l.x.update(alpha); _state.cheek_l.y.update(alpha); _state.cheek_l.radius.update(alpha); _state.cheek_l.opa.update(alpha); _state.cheek_l.color.update(alpha);
    _state.cheek_r.w.update(alpha); _state.cheek_r.h.update(alpha); _state.cheek_r.x.update(alpha); _state.cheek_r.y.update(alpha); _state.cheek_r.radius.update(alpha); _state.cheek_r.opa.update(alpha); _state.cheek_r.color.update(alpha);

    // Déplacer les pupilles (look + respiration de l'oeil)
    float elX = _state.eye_l.x.current, elY = _state.eye_l.y.current, elW = _state.eye_l.w.current, elH = _state.eye_l.h.current;
    float erX = _state.eye_r.x.current, erY = _state.eye_r.y.current, erW = _state.eye_r.w.current, erH = _state.eye_r.h.current;

    _state.pupil_l.x.set(elX + elW/2 - 11 + _lookX);
    _state.pupil_l.y.set(elY + elH/2 - 11 + _lookY);
    _state.shine_l.x.set(elX + elW - 20);
    _state.shine_l.y.set(elY + 12);

    _state.pupil_r.x.set(erX + erW/2 - 11 + _lookX);
    _state.pupil_r.y.set(erY + erH/2 - 11 + _lookY);
    _state.shine_r.x.set(erX + erW - 20);
    _state.shine_r.y.set(erY + 12);

    // Appliquer aux objets LVGL
    _applyElementToObj(_eye_left, _state.eye_l, breathOffY);
    _applyElementToObj(_eye_right, _state.eye_r, breathOffY);
    _applyElementToObj(_pupil_left, _state.pupil_l, breathOffY);
    _applyElementToObj(_pupil_right, _state.pupil_r, breathOffY);
    _applyElementToObj(_shine_left, _state.shine_l, breathOffY);
    _applyElementToObj(_shine_right, _state.shine_r, breathOffY);
    _applyElementToObj(_mouth_outer, _state.mouth_outer, breathOffY);
    _applyElementToObj(_mouth_inner, _state.mouth_inner, breathOffY);
    _applyElementToObj(_cheek_left, _state.cheek_l, breathOffY);
    _applyElementToObj(_cheek_right, _state.cheek_r, breathOffY);
}

// ============================================================
//  _applyEmotion() — Dispatch
// ============================================================
void FaceRenderer::_applyEmotion(FaceEmotion emotion, bool animated) {
    _destroyDecoObjects();
    _blinking = false;

    // Remettre les joues visibles par défaut
    _state.cheek_l.opa.set(90);
    _state.cheek_r.opa.set(90);

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
    uint32_t colHex = _colorNeutreHex();

    _setEyeOpenness(1.0f, 1.0f, animated);
    _state.eye_l.color.set(colHex);
    _state.eye_r.color.set(colHex);
    _state.eye_l.radius.set(16);
    _state.eye_r.radius.set(16);

    _setElement(_state.mouth_outer, SCR_W/2 - 50, MOUTH_Y - 7, 100, 14, 7, 255, colHex);
    _setElement(_state.mouth_inner, SCR_W/2 - 40, MOUTH_Y - 7 + 14, 80, 0, 0, 0, 0xFF2020);
}

// ============================================================
//  _applyEcoute() — Vert, yeux agrandis, anneau pulsant
// ============================================================
void FaceRenderer::_applyEcoute(bool animated) {
    uint32_t colHex = _colorEcouteHex();
    lv_color_t col = lv_color_hex(colHex);

    _setEyeOpenness(1.15f, 1.15f, animated);
    _state.eye_l.color.set(colHex);
    _state.eye_r.color.set(colHex);
    _state.eye_l.radius.set(20);
    _state.eye_r.radius.set(20);

    _setElement(_state.mouth_outer, SCR_W/2 - 40, MOUTH_Y - 11, 80, 22, 11, 255, colHex);
    _setElement(_state.mouth_inner, SCR_W/2 - 30, MOUTH_Y + 11 - 8 - 4, 60, 8, 4, 255, 0xFF2020);

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
    uint32_t colHex = _colorReflexionHex();

    _state.eye_l.w.set(EYE_W + 10); _state.eye_l.h.set(EYE_H + 10);
    _state.eye_l.x.set(EYE_LX - (EYE_W+10)/2); _state.eye_l.y.set(EYE_Y - (EYE_H+10)/2);
    _state.eye_l.color.set(colHex); _state.eye_l.radius.set(100);

    _state.eye_r.w.set(EYE_W); _state.eye_r.h.set(12);
    _state.eye_r.x.set(EYE_RX - EYE_W/2); _state.eye_r.y.set(EYE_Y - 6);
    _state.eye_r.color.set(colHex); _state.eye_r.radius.set(6);

    _state.pupil_r.opa.set(0); _state.shine_r.opa.set(0);
    _state.pupil_l.opa.set(200); _state.shine_l.opa.set(220);

    _setElement(_state.mouth_outer, SCR_W/2 - 50, MOUTH_Y - 5, 100, 10, 5, 255, colHex);
    _state.mouth_inner.opa.set(0);

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
    uint32_t colHex = _colorParleHex();

    _setEyeOpenness(1.0f, 1.0f, animated);
    _state.eye_l.color.set(colHex);
    _state.eye_r.color.set(colHex);
    _state.eye_l.radius.set(16);
    _state.eye_r.radius.set(16);

    _setElement(_state.mouth_outer, SCR_W/2 - 45, MOUTH_Y - 17, 90, 34, 17, 255, colHex);
    _setElement(_state.mouth_inner, SCR_W/2 - 35, MOUTH_Y + 17 - 6 - 4, 70, 6, 10, 255, 0xFF2020);

    _smoothRMS = 0.0f;
}

// ============================================================
//  _applyErreur() — Rouge, yeux en X, bouche inversée
// ============================================================
void FaceRenderer::_applyErreur(bool animated) {
    uint32_t colHex = _colorErreurHex();
    lv_color_t col = lv_color_hex(colHex);

    _state.eye_l.opa.set(0); _state.eye_r.opa.set(0);
    _state.pupil_l.opa.set(0); _state.pupil_r.opa.set(0);
    _state.shine_l.opa.set(0); _state.shine_r.opa.set(0);
    _state.cheek_l.opa.set(0); _state.cheek_r.opa.set(0);

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
    _setElement(_state.mouth_outer, SCR_W/2 - 40, MOUTH_Y, 80, 20, 10, 255, colHex);
    _state.mouth_inner.opa.set(0);
}

// ============================================================
//  _setEyeOpenness() — Redimensionne les yeux en hauteur
//  scale : 0.0 = fermé, 1.0 = normal, 1.15 = agrandi
// ============================================================
void FaceRenderer::_setEyeOpenness(float left, float right, bool animated) {
    float lh = max(4.0f, EYE_H * left);
    float ly = EYE_Y - lh / 2.0f;
    _state.eye_l.w.set(EYE_W); _state.eye_l.h.set(lh);
    _state.eye_l.x.set(EYE_LX - EYE_W/2.0f); _state.eye_l.y.set(ly);
    _state.eye_l.opa.set(left > 0.05f ? 255 : 0);
    _state.pupil_l.opa.set(left > 0.3f ? 200 : 0);
    _state.shine_l.opa.set(left > 0.3f ? 220 : 0);

    float rh = max(4.0f, EYE_H * right);
    float ry = EYE_Y - rh / 2.0f;
    _state.eye_r.w.set(EYE_W); _state.eye_r.h.set(rh);
    _state.eye_r.x.set(EYE_RX - EYE_W/2.0f); _state.eye_r.y.set(ry);
    _state.eye_r.opa.set(right > 0.05f ? 255 : 0);
    _state.pupil_r.opa.set(right > 0.3f ? 200 : 0);
    _state.shine_r.opa.set(right > 0.3f ? 220 : 0);
}

// ============================================================
//  _setMouthOpen() — Anime l'ouverture de la bouche
//  openness : 0.0 = fermée, 1.0 = grande ouverte
// ============================================================
void FaceRenderer::_setMouthOpen(float openness) {
    openness = max(0.0f, min(1.0f, openness));
    float innerH = openness * 24.0f;
    _state.mouth_inner.h.set(innerH);
    _state.mouth_inner.y.set(MOUTH_Y + 17 - innerH - 4);
    _state.mouth_inner.opa.set(openness > 0.05f ? 255 : 0);
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
