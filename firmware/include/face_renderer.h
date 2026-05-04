#ifndef FACE_RENDERER_H
#define FACE_RENDERER_H

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// ============================================================
//  FaceRenderer v4.0 — "Neon Soul"
//  Visage vectoriel LVGL premium pour Robot IA
//
//  Nouveautés v4 :
//  - Paupières (eyelid) pour clignement naturel (le globe reste fixe)
//  - Reflets secondaires sur chaque œil (double highlight = yeux vivants)
//  - Lip-sync asymétrique (attack rapide / release lent)
//  - Bouche avec ouverture + élargissement dynamique
//  - Gradient sur les yeux (LVGL bg_grad)
//  - Palette de couleurs harmonieuse et premium
// ============================================================

// --- États du visage ---
enum class FaceEmotion : uint8_t {
    NEUTRE    = 0,
    ECOUTE    = 1,
    REFLEXION = 2,
    PARLE     = 3,
    ERREUR    = 4
};

// --- Configuration fine ---
struct FaceConfig {
    // Timing
    uint16_t blink_interval_min = 2500;
    uint16_t blink_interval_max = 5000;
    uint16_t blink_close_ms     = 80;    // Fermeture rapide
    uint16_t blink_open_ms      = 140;   // Ouverture plus lente (naturel)
    uint16_t transition_ms      = 200;

    // Lip-sync asymétrique
    float    mouth_attack_ms    = 25.0f;  // Bouche s'ouvre vite (25ms)
    float    mouth_release_ms   = 90.0f;  // Bouche se ferme doucement (90ms)
    float    rms_min_open       = 0.03f;
    float    rms_max_open       = 0.35f;
    float    rms_scale          = 6.0f;
    float    mouth_width_scale  = 0.15f;  // Bouche s'élargit quand ouverte

    // Visuel
    uint8_t  pupil_size         = 28;
    uint16_t cheek_alpha        = 80;
};

// --- Interpolation Helpers ---
struct SmoothValue {
    float current = 0.0f;
    float target = 0.0f;
    void update(float alpha) { current += alpha * (target - current); }
    void set(float val) { target = val; }
    void snap(float val) { target = val; current = val; }
};

struct SmoothColor {
    float r_curr = 0, g_curr = 0, b_curr = 0;
    float r_targ = 0, g_targ = 0, b_targ = 0;
    void update(float alpha) {
        r_curr += alpha * (r_targ - r_curr);
        g_curr += alpha * (g_targ - g_curr);
        b_curr += alpha * (b_targ - b_curr);
    }
    void set(uint32_t hex24) {
        r_targ = (float)((hex24 >> 16) & 0xFF);
        g_targ = (float)((hex24 >> 8) & 0xFF);
        b_targ = (float)(hex24 & 0xFF);
    }
    void snap(uint32_t hex24) {
        set(hex24);
        r_curr = r_targ; g_curr = g_targ; b_curr = b_targ;
    }
    lv_color_t get() const {
        return lv_color_make((uint8_t)r_curr, (uint8_t)g_curr, (uint8_t)b_curr);
    }
};

struct ElementState {
    SmoothValue w, h, x, y, opa, radius;
    SmoothColor color;
};

struct FaceState {
    ElementState eye_l, eye_r;
    ElementState pupil_l, pupil_r;
    ElementState shine_l, shine_r;
    ElementState shine2_l, shine2_r;   // Reflet secondaire
    ElementState eyelid_l, eyelid_r;   // Paupière
    ElementState mouth_outer, mouth_inner;
    ElementState cheek_l, cheek_r;
};

// ============================================================
//  Classe principale
// ============================================================
class FaceRenderer {
public:
    FaceRenderer();
    ~FaceRenderer();

    void init(TFT_eSPI* tft);
    void deinit();

    void setEmotion(FaceEmotion emotion);
    void setEmotionFromString(const String& name);
    void setAudioRMS(float rms);
    void tick(uint32_t delta_ms);

    void _applyEmotion(FaceEmotion emotion, bool animated);

    FaceConfig config;
    FaceEmotion currentEmotion() const { return _emotion; }
    bool isInitialized() const { return _initialized; }

private:
    TFT_eSPI*          _tft         = nullptr;
    lv_disp_draw_buf_t* _draw_buf  = nullptr;
    lv_color_t*        _buf1       = nullptr;
    lv_disp_drv_t*     _disp_drv   = nullptr;
    lv_disp_t*         _disp       = nullptr;
    bool               _initialized = false;

    FaceEmotion _emotion      = FaceEmotion::NEUTRE;
    FaceEmotion _prevEmotion  = FaceEmotion::NEUTRE;
    float       _audioRMS     = 0.0f;
    float       _smoothRMS    = 0.0f;
    float       _mouthOpen    = 0.0f;    // Ouverture courante (lissée)
    uint32_t    _lastBlink    = 0;
    uint32_t    _nextBlinkIn  = 3000;    // Intervalle aléatoire
    bool        _blinking     = false;
    uint32_t    _blinkStart   = 0;
    bool        _blinkIsDouble = false;
    uint8_t     _blinkPhase   = 0;
    uint32_t    _elapsed      = 0;
    uint32_t    _questionPhase = 0;
    float       _breathPhase  = 0.0f;
    float       _lookX        = 0.0f;
    float       _lookY        = 0.0f;
    uint32_t    _lastLookTime = 0;

    FaceState   _state;

    // Objets LVGL
    lv_obj_t* _screen       = nullptr;
    lv_obj_t* _bg           = nullptr;

    lv_obj_t* _eye_left     = nullptr;
    lv_obj_t* _eye_right    = nullptr;
    lv_obj_t* _pupil_left   = nullptr;
    lv_obj_t* _pupil_right  = nullptr;
    lv_obj_t* _shine_left   = nullptr;
    lv_obj_t* _shine_right  = nullptr;
    lv_obj_t* _shine2_left  = nullptr;
    lv_obj_t* _shine2_right = nullptr;
    lv_obj_t* _eyelid_left  = nullptr;
    lv_obj_t* _eyelid_right = nullptr;

    lv_obj_t* _mouth_outer  = nullptr;
    lv_obj_t* _mouth_inner  = nullptr;

    lv_obj_t* _cheek_left   = nullptr;
    lv_obj_t* _cheek_right  = nullptr;

    lv_obj_t* _deco_1       = nullptr;
    lv_obj_t* _deco_2       = nullptr;
    lv_obj_t* _scan_ring    = nullptr;

    // Méthodes privées
    void _buildBaseObjects();
    void _destroyDecoObjects();
    void _applyNeutre(bool animated);
    void _applyEcoute(bool animated);
    void _applyReflexion(bool animated);
    void _applyParle(bool animated);
    void _applyErreur(bool animated);

    void _setEyeOpenness(float left, float right);
    void _updateBlink(uint32_t now);
    void _updateLipSync(uint32_t delta_ms);
    void _updateReflexionAnim(uint32_t delta_ms);
    void _updateScanRing(uint32_t delta_ms);
    void _updateAllElements(float alpha, float breathY);

    // Palette "Neon Soul" — Couleurs premium harmonisées
    static uint32_t _colNeutreHex()    { return 0x88D4F2; } // Bleu ciel lumineux
    static uint32_t _colEcouteHex()    { return 0x7EEFC4; } // Menthe néon
    static uint32_t _colReflexionHex() { return 0xB88CF7; } // Lavande électrique
    static uint32_t _colParleHex()     { return 0xFFA07A; } // Saumon chaud
    static uint32_t _colErreurHex()    { return 0xF76C6C; } // Rouge corail
    static uint32_t _colBgHex()        { return 0x080E18; } // Nuit profonde
    static uint32_t _colPupilHex()     { return 0x0C1520; } // Noir bleuté
    static uint32_t _colMouthInHex()   { return 0xE8617A; } // Rose intérieur bouche
    static uint32_t _colCheekHex()     { return 0xFFADC8; } // Rose joues doux

    static lv_color_t _colBg() { return lv_color_hex(_colBgHex()); }

    static const uint16_t SCR_W = 320;
    static const uint16_t SCR_H = 240;
    static const uint16_t EYE_W = 76;
    static const uint16_t EYE_H = 76;
    static const uint16_t EYE_LX = 90;
    static const uint16_t EYE_RX = 230;
    static const uint16_t EYE_Y  = 95;
    static const uint16_t MOUTH_Y = 185;
};

// ============================================================
//  Utilitaire RMS pour speakerTask
// ============================================================
inline float computeRMS(const uint8_t* buffer, size_t length) {
    if (length < 2) return 0.0f;
    const int16_t* samples = reinterpret_cast<const int16_t*>(buffer);
    size_t count = length / 2;
    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        float s = samples[i] / 32768.0f;
        sum += s * s;
    }
    return sqrtf(sum / count);
}

#endif // FACE_RENDERER_H
