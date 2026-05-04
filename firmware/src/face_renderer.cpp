#include "face_renderer.h"

// ============================================================
//  LVGL Driver Callbacks
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
FaceRenderer::~FaceRenderer() { if (_initialized) deinit(); }

// --- Helpers pour ElementState ---
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
//  init()
// ============================================================
void FaceRenderer::init(TFT_eSPI* tft) {
    if (_initialized) return;
    _tft = tft;
    _g_tft = tft;

    // Buffer mémoire pour LVGL (10 lignes)
    _buf1 = (lv_color_t*)malloc(SCR_W * 10 * sizeof(lv_color_t));
    _draw_buf = (lv_disp_draw_buf_t*)malloc(sizeof(lv_disp_draw_buf_t));
    _disp_drv = (lv_disp_drv_t*)malloc(sizeof(lv_disp_drv_t));

    if (!_buf1 || !_draw_buf || !_disp_drv) return;

    lv_disp_draw_buf_init(_draw_buf, _buf1, NULL, SCR_W * 10);
    lv_disp_drv_init(_disp_drv);
    _disp_drv->hor_res  = SCR_W;
    _disp_drv->ver_res  = SCR_H;
    _disp_drv->flush_cb = _face_disp_flush;
    _disp_drv->draw_buf = _draw_buf;
    _disp = lv_disp_drv_register(_disp_drv);

    _screen = lv_disp_get_scr_act(_disp);
    lv_obj_set_style_bg_color(_screen, _colBg(), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    _buildBaseObjects();

    uint32_t cN = _colNeutreHex();
    
    // Initialisation géométrie (snap)
    _snapElement(_state.eye_l, EYE_LX - EYE_W/2, EYE_Y - EYE_H/2, EYE_W, EYE_H, 20, 255, cN);
    _snapElement(_state.eye_r, EYE_RX - EYE_W/2, EYE_Y - EYE_H/2, EYE_W, EYE_H, 20, 255, cN);
    
    _snapElement(_state.pupil_l, EYE_LX - config.pupil_size/2, EYE_Y - config.pupil_size/2, config.pupil_size, config.pupil_size, config.pupil_size/2, 255, _colPupilHex());
    _snapElement(_state.pupil_r, EYE_RX - config.pupil_size/2, EYE_Y - config.pupil_size/2, config.pupil_size, config.pupil_size, config.pupil_size/2, 255, _colPupilHex());
    
    _snapElement(_state.shine_l, EYE_LX - 6, EYE_Y - 10, 10, 10, 5, 230, 0xFFFFFF);
    _snapElement(_state.shine_r, EYE_RX - 6, EYE_Y - 10, 10, 10, 5, 230, 0xFFFFFF);
    
    _snapElement(_state.shine2_l, EYE_LX + 4, EYE_Y + 6, 4, 4, 2, 180, 0xFFFFFF);
    _snapElement(_state.shine2_r, EYE_RX + 4, EYE_Y + 6, 4, 4, 2, 180, 0xFFFFFF);
    
    _snapElement(_state.eyelid_l, EYE_LX - EYE_W/2 - 2, EYE_Y - EYE_H/2 - 5, EYE_W + 4, 0, 10, 255, _colBgHex());
    _snapElement(_state.eyelid_r, EYE_RX - EYE_W/2 - 2, EYE_Y - EYE_H/2 - 5, EYE_W + 4, 0, 10, 255, _colBgHex());

    _snapElement(_state.mouth_outer, SCR_W/2 - 40, MOUTH_Y - 5, 80, 10, 5, 255, cN);
    _snapElement(_state.mouth_inner, SCR_W/2 - 30, MOUTH_Y, 60, 0, 0, 0, _colMouthInHex());
    
    _snapElement(_state.cheek_l, 25, 125, 45, 25, 12, config.cheek_alpha, _colCheekHex());
    _snapElement(_state.cheek_r, 250, 125, 45, 25, 12, config.cheek_alpha, _colCheekHex());

    _applyEmotion(FaceEmotion::NEUTRE, false);

    _initialized = true;
}

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
//  _buildBaseObjects()
// ============================================================
static lv_obj_t* _createObj(lv_obj_t* parent, bool scroll=false) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_style_border_width(obj, 0, 0);
    if (!scroll) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    }
    return obj;
}

void FaceRenderer::_buildBaseObjects() {
    _bg = _createObj(_screen);
    lv_obj_set_size(_bg, SCR_W, SCR_H);
    lv_obj_set_pos(_bg, 0, 0);
    lv_obj_set_style_bg_color(_bg, _colBg(), 0);
    lv_obj_set_style_bg_opa(_bg, LV_OPA_COVER, 0);

    _cheek_left   = _createObj(_screen);
    _cheek_right  = _createObj(_screen);
    
    _eye_left     = _createObj(_screen);
    _eye_right    = _createObj(_screen);
    
    _pupil_left   = _createObj(_screen);
    _pupil_right  = _createObj(_screen);
    
    _shine_left   = _createObj(_screen);
    _shine_right  = _createObj(_screen);
    _shine2_left  = _createObj(_screen);
    _shine2_right = _createObj(_screen);
    
    _mouth_outer  = _createObj(_screen);
    _mouth_inner  = _createObj(_screen);
    
    // Eyelids drawn ON TOP of eyes, pupils and shines
    _eyelid_left  = _createObj(_screen);
    _eyelid_right = _createObj(_screen);
}

void FaceRenderer::_destroyDecoObjects() {
    if (_deco_1)     { lv_obj_del(_deco_1);     _deco_1 = nullptr; }
    if (_deco_2)     { lv_obj_del(_deco_2);     _deco_2 = nullptr; }
    if (_scan_ring)  { lv_obj_del(_scan_ring);  _scan_ring = nullptr; }
}

// ============================================================
//  setEmotion() & async callback
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

    EmotionCallArg* arg = (EmotionCallArg*)malloc(sizeof(EmotionCallArg));
    if (arg) {
        arg->self    = this;
        arg->emotion = emotion;
        lv_async_call(_emotionCallback, arg);
    }
}

void FaceRenderer::setEmotionFromString(const String& name) {
    if      (name == "neutre"    || name == "idle")      setEmotion(FaceEmotion::NEUTRE);
    else if (name == "ecoute"    || name == "listening") setEmotion(FaceEmotion::ECOUTE);
    else if (name == "reflexion" || name == "thinking")  setEmotion(FaceEmotion::REFLEXION);
    else if (name == "parle"     || name == "speaking")  setEmotion(FaceEmotion::PARLE);
    else if (name == "erreur"    || name == "error")     setEmotion(FaceEmotion::ERREUR);
}

void FaceRenderer::setAudioRMS(float rms) { _audioRMS = rms; }

// ============================================================
//  tick() — Main Animation Loop
// ============================================================
void FaceRenderer::tick(uint32_t delta_ms) {
    if (!_initialized) return;
    uint32_t now = millis();
    _elapsed += delta_ms;

    // Respiration
    _breathPhase += delta_ms;
    float breathOffY = sinf((float)_breathPhase / 1500.0f * 2.0f * 3.14159f) * 3.0f; // ±3px

    // Eye procedural look (slower, more fluid)
    if (now - _lastLookTime > 2500 && random(100) < 15) {
        _lastLookTime = now;
        int mode = random(4);
        if      (mode == 0) { _lookX =  8.0f; _lookY =  0.0f; }
        else if (mode == 1) { _lookX = -8.0f; _lookY =  0.0f; }
        else if (mode == 2) { _lookX =  0.0f; _lookY = -6.0f; }
        else                { _lookX = (random(160)-80)/100.0f*8.0f;
                              _lookY = (random(100)-50)/100.0f*5.0f; }
    } else if (now - _lastLookTime > 4000) {
        _lookX *= 0.90f; _lookY *= 0.90f; // fluid return to center
    }

    if (_emotion != FaceEmotion::ERREUR && _emotion != FaceEmotion::PARLE) {
        _updateBlink(now);
    }

    if (_emotion == FaceEmotion::PARLE)     _updateLipSync(delta_ms);
    if (_emotion == FaceEmotion::REFLEXION) _updateReflexionAnim(delta_ms);
    if (_emotion == FaceEmotion::ECOUTE)    _updateScanRing(delta_ms);

    // Lissage
    float alpha = 1.0f - expf(-(float)delta_ms / 50.0f);
    _updateAllElements(alpha, breathOffY);
}

void FaceRenderer::_updateAllElements(float alpha, float breathY) {
    // 1. Update values
    _state.eye_l.w.update(alpha); _state.eye_l.h.update(alpha); _state.eye_l.x.update(alpha); _state.eye_l.y.update(alpha); _state.eye_l.radius.update(alpha); _state.eye_l.opa.update(alpha); _state.eye_l.color.update(alpha);
    _state.eye_r.w.update(alpha); _state.eye_r.h.update(alpha); _state.eye_r.x.update(alpha); _state.eye_r.y.update(alpha); _state.eye_r.radius.update(alpha); _state.eye_r.opa.update(alpha); _state.eye_r.color.update(alpha);
    _state.pupil_l.w.update(alpha); _state.pupil_l.h.update(alpha); _state.pupil_l.x.update(alpha); _state.pupil_l.y.update(alpha); _state.pupil_l.radius.update(alpha); _state.pupil_l.opa.update(alpha); _state.pupil_l.color.update(alpha);
    _state.pupil_r.w.update(alpha); _state.pupil_r.h.update(alpha); _state.pupil_r.x.update(alpha); _state.pupil_r.y.update(alpha); _state.pupil_r.radius.update(alpha); _state.pupil_r.opa.update(alpha); _state.pupil_r.color.update(alpha);
    _state.shine_l.w.update(alpha); _state.shine_l.h.update(alpha); _state.shine_l.x.update(alpha); _state.shine_l.y.update(alpha); _state.shine_l.radius.update(alpha); _state.shine_l.opa.update(alpha); _state.shine_l.color.update(alpha);
    _state.shine_r.w.update(alpha); _state.shine_r.h.update(alpha); _state.shine_r.x.update(alpha); _state.shine_r.y.update(alpha); _state.shine_r.radius.update(alpha); _state.shine_r.opa.update(alpha); _state.shine_r.color.update(alpha);
    _state.shine2_l.w.update(alpha); _state.shine2_l.h.update(alpha); _state.shine2_l.x.update(alpha); _state.shine2_l.y.update(alpha); _state.shine2_l.radius.update(alpha); _state.shine2_l.opa.update(alpha); _state.shine2_l.color.update(alpha);
    _state.shine2_r.w.update(alpha); _state.shine2_r.h.update(alpha); _state.shine2_r.x.update(alpha); _state.shine2_r.y.update(alpha); _state.shine2_r.radius.update(alpha); _state.shine2_r.opa.update(alpha); _state.shine2_r.color.update(alpha);
    _state.eyelid_l.w.update(alpha); _state.eyelid_l.h.update(alpha); _state.eyelid_l.x.update(alpha); _state.eyelid_l.y.update(alpha); _state.eyelid_l.radius.update(alpha); _state.eyelid_l.opa.update(alpha); _state.eyelid_l.color.update(alpha);
    _state.eyelid_r.w.update(alpha); _state.eyelid_r.h.update(alpha); _state.eyelid_r.x.update(alpha); _state.eyelid_r.y.update(alpha); _state.eyelid_r.radius.update(alpha); _state.eyelid_r.opa.update(alpha); _state.eyelid_r.color.update(alpha);
    _state.mouth_outer.w.update(alpha); _state.mouth_outer.h.update(alpha); _state.mouth_outer.x.update(alpha); _state.mouth_outer.y.update(alpha); _state.mouth_outer.radius.update(alpha); _state.mouth_outer.opa.update(alpha); _state.mouth_outer.color.update(alpha);
    _state.mouth_inner.w.update(alpha); _state.mouth_inner.h.update(alpha); _state.mouth_inner.x.update(alpha); _state.mouth_inner.y.update(alpha); _state.mouth_inner.radius.update(alpha); _state.mouth_inner.opa.update(alpha); _state.mouth_inner.color.update(alpha);
    _state.cheek_l.w.update(alpha); _state.cheek_l.h.update(alpha); _state.cheek_l.x.update(alpha); _state.cheek_l.y.update(alpha); _state.cheek_l.radius.update(alpha); _state.cheek_l.opa.update(alpha); _state.cheek_l.color.update(alpha);
    _state.cheek_r.w.update(alpha); _state.cheek_r.h.update(alpha); _state.cheek_r.x.update(alpha); _state.cheek_r.y.update(alpha); _state.cheek_r.radius.update(alpha); _state.cheek_r.opa.update(alpha); _state.cheek_r.color.update(alpha);

    // 2. Position dynamic elements (Pupils & Shines follow look)
    float elX = _state.eye_l.x.current, elY = _state.eye_l.y.current, elW = _state.eye_l.w.current, elH = _state.eye_l.h.current;
    float erX = _state.eye_r.x.current, erY = _state.eye_r.y.current, erW = _state.eye_r.w.current, erH = _state.eye_r.h.current;

    _state.pupil_l.x.set(elX + elW/2 - config.pupil_size/2 + _lookX);
    _state.pupil_l.y.set(elY + elH/2 - config.pupil_size/2 + _lookY);
    _state.shine_l.x.set(elX + elW/2 + 6 + _lookX);
    _state.shine_l.y.set(elY + elH/2 - 12 + _lookY);
    _state.shine2_l.x.set(elX + elW/2 - 12 + _lookX);
    _state.shine2_l.y.set(elY + elH/2 + 8 + _lookY);

    _state.pupil_r.x.set(erX + erW/2 - config.pupil_size/2 + _lookX);
    _state.pupil_r.y.set(erY + erH/2 - config.pupil_size/2 + _lookY);
    _state.shine_r.x.set(erX + erW/2 + 6 + _lookX);
    _state.shine_r.y.set(erY + erH/2 - 12 + _lookY);
    _state.shine2_r.x.set(erX + erW/2 - 12 + _lookX);
    _state.shine2_r.y.set(erY + erH/2 + 8 + _lookY);

    // 3. Apply to LVGL objects
    _applyElementToObj(_eye_left,     _state.eye_l,     breathY);
    _applyElementToObj(_eye_right,    _state.eye_r,     breathY);
    _applyElementToObj(_pupil_left,   _state.pupil_l,   breathY);
    _applyElementToObj(_pupil_right,  _state.pupil_r,   breathY);
    _applyElementToObj(_shine_left,   _state.shine_l,   breathY);
    _applyElementToObj(_shine_right,  _state.shine_r,   breathY);
    _applyElementToObj(_shine2_left,  _state.shine2_l,  breathY);
    _applyElementToObj(_shine2_right, _state.shine2_r,  breathY);
    _applyElementToObj(_eyelid_left,  _state.eyelid_l,  breathY);
    _applyElementToObj(_eyelid_right, _state.eyelid_r,  breathY);
    _applyElementToObj(_mouth_outer,  _state.mouth_outer, breathY);
    _applyElementToObj(_mouth_inner,  _state.mouth_inner, breathY);
    _applyElementToObj(_cheek_left,   _state.cheek_l,   breathY);
    _applyElementToObj(_cheek_right,  _state.cheek_r,   breathY);
}

// ============================================================
//  Emotions
// ============================================================
void FaceRenderer::_applyEmotion(FaceEmotion emotion, bool animated) {
    _destroyDecoObjects();
    _blinking = false;

    _state.cheek_l.opa.set(config.cheek_alpha);
    _state.cheek_r.opa.set(config.cheek_alpha);

    switch (emotion) {
        case FaceEmotion::NEUTRE:    _applyNeutre(animated);    break;
        case FaceEmotion::ECOUTE:    _applyEcoute(animated);    break;
        case FaceEmotion::REFLEXION: _applyReflexion(animated); break;
        case FaceEmotion::PARLE:     _applyParle(animated);     break;
        case FaceEmotion::ERREUR:    _applyErreur(animated);    break;
    }
}

void FaceRenderer::_applyNeutre(bool animated) {
    uint32_t col = _colNeutreHex();
    _setEyeOpenness(1.0f, 1.0f);
    _state.eye_l.color.set(col); _state.eye_r.color.set(col);
    _state.eye_l.radius.set(22); _state.eye_r.radius.set(22);

    _setElement(_state.mouth_outer, SCR_W/2 - 40, MOUTH_Y - 5, 80, 10, 5, 255, col);
    _state.mouth_inner.opa.set(0);
}

void FaceRenderer::_applyEcoute(bool animated) {
    uint32_t col = _colEcouteHex();
    _setEyeOpenness(1.1f, 1.1f);
    _state.eye_l.color.set(col); _state.eye_r.color.set(col);
    _state.eye_l.radius.set(28); _state.eye_r.radius.set(28);

    _setElement(_state.mouth_outer, SCR_W/2 - 30, MOUTH_Y - 10, 60, 20, 10, 255, col);
    _state.mouth_inner.opa.set(0);

    _scan_ring = _createObj(_screen);
    lv_obj_set_size(_scan_ring, SCR_W - 6, SCR_H - 6);
    lv_obj_set_pos(_scan_ring, 3, 3);
    lv_obj_set_style_bg_opa(_scan_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(_scan_ring, lv_color_hex(col), 0);
    lv_obj_set_style_border_width(_scan_ring, 3, 0);
    lv_obj_set_style_border_opa(_scan_ring, 180, 0);
    lv_obj_set_style_radius(_scan_ring, 15, 0);
    lv_obj_move_background(_scan_ring);
}

void FaceRenderer::_applyReflexion(bool animated) {
    uint32_t col = _colReflexionHex();

    _state.eye_l.w.set(EYE_W + 8); _state.eye_l.h.set(EYE_H + 8);
    _state.eye_l.x.set(EYE_LX - (EYE_W+8)/2); _state.eye_l.y.set(EYE_Y - (EYE_H+8)/2);
    _state.eye_l.color.set(col); _state.eye_l.radius.set(LV_RADIUS_CIRCLE);
    _state.eyelid_l.h.set(0); _state.eyelid_l.opa.set(0);

    _state.eye_r.w.set(EYE_W); _state.eye_r.h.set(16);
    _state.eye_r.x.set(EYE_RX - EYE_W/2); _state.eye_r.y.set(EYE_Y - 8);
    _state.eye_r.color.set(col); _state.eye_r.radius.set(8);
    _state.eyelid_r.h.set(0); _state.eyelid_r.opa.set(0);

    _state.pupil_r.opa.set(0); _state.shine_r.opa.set(0); _state.shine2_r.opa.set(0);
    _state.pupil_l.opa.set(255); _state.shine_l.opa.set(230); _state.shine2_l.opa.set(180);

    _setElement(_state.mouth_outer, SCR_W/2 - 40, MOUTH_Y, 80, 10, 5, 255, col);
    _state.mouth_inner.opa.set(0);

    _deco_1 = lv_label_create(_screen);
    lv_label_set_text(_deco_1, "?");
    lv_obj_set_style_text_color(_deco_1, lv_color_hex(0xFFDD55), 0);
    lv_obj_set_style_text_font(_deco_1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(_deco_1, 250, 15);

    _deco_2 = _createObj(_screen);
    lv_obj_set_size(_deco_2, 12, 12);
    lv_obj_set_pos(_deco_2, 35, 35);
    lv_obj_set_style_bg_color(_deco_2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_deco_2, 160, 0);
    lv_obj_set_style_radius(_deco_2, LV_RADIUS_CIRCLE, 0);

    _questionPhase = 0;
}

void FaceRenderer::_applyParle(bool animated) {
    uint32_t col = _colParleHex();
    _setEyeOpenness(1.0f, 1.0f);
    _state.eye_l.color.set(col); _state.eye_r.color.set(col);
    _state.eye_l.radius.set(22); _state.eye_r.radius.set(22);

    _setElement(_state.mouth_outer, SCR_W/2 - 50, MOUTH_Y - 5, 100, 10, 5, 255, col);
    _setElement(_state.mouth_inner, SCR_W/2 - 40, MOUTH_Y - 1, 80, 0, 0, 0, _colMouthInHex());

    _smoothRMS = 0.0f;
    _mouthOpen = 0.0f;
}

void FaceRenderer::_applyErreur(bool animated) {
    uint32_t col = _colErreurHex();

    _state.eye_l.opa.set(0); _state.eye_r.opa.set(0);
    _state.pupil_l.opa.set(0); _state.pupil_r.opa.set(0);
    _state.shine_l.opa.set(0); _state.shine_r.opa.set(0);
    _state.shine2_l.opa.set(0); _state.shine2_r.opa.set(0);
    _state.eyelid_l.opa.set(0); _state.eyelid_r.opa.set(0);
    _state.cheek_l.opa.set(0); _state.cheek_r.opa.set(0);

    _deco_1 = lv_label_create(_screen);
    lv_label_set_text(_deco_1, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(_deco_1, lv_color_hex(col), 0);
    lv_obj_set_style_text_font(_deco_1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(_deco_1, EYE_LX - 30, EYE_Y - 30);

    _deco_2 = lv_label_create(_screen);
    lv_label_set_text(_deco_2, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(_deco_2, lv_color_hex(col), 0);
    lv_obj_set_style_text_font(_deco_2, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(_deco_2, EYE_RX - 30, EYE_Y - 30);

    _setElement(_state.mouth_outer, SCR_W/2 - 40, MOUTH_Y + 10, 80, 16, 8, 255, col);
    _state.mouth_inner.opa.set(0);
}

// ============================================================
//  Blinking (Eyelids)
// ============================================================
void FaceRenderer::_setEyeOpenness(float left, float right) {
    if (_emotion == FaceEmotion::REFLEXION || _emotion == FaceEmotion::ERREUR) return;

    _state.eye_l.w.set(EYE_W); _state.eye_l.h.set(EYE_H);
    _state.eye_l.x.set(EYE_LX - EYE_W/2); _state.eye_l.y.set(EYE_Y - EYE_H/2);
    _state.eye_r.w.set(EYE_W); _state.eye_r.h.set(EYE_H);
    _state.eye_r.x.set(EYE_RX - EYE_W/2); _state.eye_r.y.set(EYE_Y - EYE_H/2);

    _state.eye_l.opa.set(255); _state.eye_r.opa.set(255);
    _state.pupil_l.opa.set(255); _state.pupil_r.opa.set(255);
    _state.shine_l.opa.set(230); _state.shine_r.opa.set(230);
    _state.shine2_l.opa.set(180); _state.shine2_r.opa.set(180);

    // Eyelid closes from top to bottom
    float lid_lh = EYE_H * (1.0f - left);
    float lid_rh = EYE_H * (1.0f - right);
    
    _state.eyelid_l.h.set(lid_lh + 5);
    _state.eyelid_r.h.set(lid_rh + 5);
    _state.eyelid_l.opa.set(lid_lh > 1.0f ? 255 : 0);
    _state.eyelid_r.opa.set(lid_rh > 1.0f ? 255 : 0);
}

void FaceRenderer::_updateBlink(uint32_t now) {
    if (!_blinking) {
        if (now - _lastBlink > _nextBlinkIn) {
            _blinking      = true;
            _blinkStart    = now;
            _blinkIsDouble = (random(100) < 15);
            _blinkPhase    = 0;
            _setEyeOpenness(0.0f, 0.0f);
        }
    } else {
        uint32_t elapsed = now - _blinkStart;
        if (_blinkIsDouble && _blinkPhase == 0 && elapsed > config.blink_close_ms) {
            _blinkPhase = 1; _blinkStart = now;
            _setEyeOpenness(1.0f, 1.0f);
        } else if (_blinkIsDouble && _blinkPhase == 1 && elapsed > 80) {
            _blinkPhase = 2; _blinkStart = now;
            _setEyeOpenness(0.0f, 0.0f);
        } else if (elapsed > config.blink_close_ms && _blinkPhase != 1) {
            if (_blinkPhase != 3) {
                _blinkPhase = 3;
                _blinkStart = now;
                _setEyeOpenness(1.0f, 1.0f);
            } else if (elapsed > config.blink_open_ms) {
                _blinking  = false;
                _lastBlink = now;
                _nextBlinkIn = random(config.blink_interval_min, config.blink_interval_max);
            }
        }
    }
}

// ============================================================
//  Lip Sync (Asymmetric)
// ============================================================
void FaceRenderer::_updateLipSync(uint32_t delta_ms) {
    // Attack / Release asymétrique
    float attack_alpha = 1.0f - expf(-(float)delta_ms / config.mouth_attack_ms);
    float release_alpha = 1.0f - expf(-(float)delta_ms / config.mouth_release_ms);

    if (_audioRMS > _smoothRMS) _smoothRMS += attack_alpha * (_audioRMS - _smoothRMS);
    else                        _smoothRMS += release_alpha * (_audioRMS - _smoothRMS);

    float target_open = 0.0f;
    if (_smoothRMS > config.rms_min_open) {
        target_open = (_smoothRMS - config.rms_min_open) / (config.rms_max_open - config.rms_min_open);
        target_open = min(1.0f, target_open * config.rms_scale);
    }
    
    // Smoothing on the openness directly
    _mouthOpen += 0.4f * (target_open - _mouthOpen);

    // Mouth dimensions
    float baseW = 100.0f;
    float maxW  = baseW + (baseW * config.mouth_width_scale);
    float outerH = 10.0f + (_mouthOpen * 36.0f);
    float outerW = baseW + (_mouthOpen * (maxW - baseW));
    
    _state.mouth_outer.w.set(outerW);
    _state.mouth_outer.h.set(outerH);
    _state.mouth_outer.x.set(SCR_W/2 - outerW/2);
    _state.mouth_outer.y.set(MOUTH_Y - outerH/2);
    _state.mouth_outer.radius.set(outerH/2.5f);

    float innerH = max(0.0f, outerH - 12.0f);
    float innerW = max(0.0f, outerW - 16.0f);
    _state.mouth_inner.w.set(innerW);
    _state.mouth_inner.h.set(innerH);
    _state.mouth_inner.x.set(SCR_W/2 - innerW/2);
    _state.mouth_inner.y.set(MOUTH_Y - innerH/2);
    _state.mouth_inner.radius.set(innerH/2.5f);
    _state.mouth_inner.opa.set(_mouthOpen > 0.05f ? 255 : 0);
}

// ============================================================
//  Deco Animations
// ============================================================
void FaceRenderer::_updateReflexionAnim(uint32_t delta_ms) {
    _questionPhase += delta_ms;
    if (_deco_1) {
        float t = sinf((float)_questionPhase / 800.0f * 2.0f * 3.14159f);
        lv_obj_set_pos(_deco_1, 250, 15 + (int16_t)(t * 8.0f));
    }
    if (_deco_2) {
        lv_opa_t opa = ((_questionPhase / 600) % 2 == 0) ? 180 : 40;
        lv_obj_set_style_bg_opa(_deco_2, opa, 0);
    }
}

void FaceRenderer::_updateScanRing(uint32_t delta_ms) {
    if (!_scan_ring) return;
    float t = sinf((float)_elapsed / 1200.0f * 2.0f * 3.14159f);
    lv_opa_t opa = (lv_opa_t)(130 + (int)(t * 70.0f));
    lv_obj_set_style_border_opa(_scan_ring, opa, 0);
}
