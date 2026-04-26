#ifndef FACE_RENDERER_H
#define FACE_RENDERER_H

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// ============================================================
//  FaceRenderer — Visages vectoriels LVGL pour Robot IA v3.0
//  Remplace entièrement le système BMP/SD.
//  - Zéro lecture SD, tout en RAM/flash
//  - Animations fluides via lv_anim_t
//  - Sync bouche/audio via setAudioRMS()
//  - Intégration directe avec displayTask et speakerTask
// ============================================================

// --- États du visage (miroir du project_state.json) ---
enum class FaceEmotion : uint8_t {
    NEUTRE    = 0,   // Cyan, yeux rectangles, bouche ligne
    ECOUTE    = 1,   // Vert, yeux agrandis, anneau pulsant
    REFLEXION = 2,   // Magenta, yeux asymétriques, "?" flottant
    PARLE     = 3,   // Vert menthe, bouche animée sync audio
    ERREUR    = 4    // Rouge, yeux en X, bouche inversée
};

// --- Paramètres d'animation fine ---
struct FaceConfig {
    // Timing
    uint16_t blink_interval_ms  = 3500;  // Intervalle entre clignements
    uint16_t blink_duration_ms  = 180;   // Durée d'un clignement
    uint16_t transition_ms      = 250;   // Transition entre émotions
    uint16_t mouth_smooth_ms    = 60;    // Lissage de la bouche (audio)

    // Audio → Bouche
    float    rms_scale          = 8.0f;  // Amplification du RMS pour la bouche
    float    rms_min_open       = 0.04f; // Seuil minimum pour ouvrir la bouche
    float    rms_max_open       = 0.40f; // Seuil de saturation (bouche max ouverte)

    // Visuel
    uint8_t  pupil_offset_idle  = 3;     // Pixels de déplacement oculaire en IDLE
    uint16_t cheek_blink_alpha  = 110;   // Opacité des joues (0-255)
};

// ============================================================
//  Classe principale
// ============================================================
class FaceRenderer {
public:
    FaceRenderer();
    ~FaceRenderer();

    // --- Cycle de vie (à appeler depuis displayTask) ---
    // Initialise LVGL + drivers TFT. Appeler APRES lv_init().
    void init(TFT_eSPI* tft);

    // Libère toutes les ressources LVGL. Appeler avant deinit du display.
    void deinit();

    // --- Contrôle de l'émotion ---
    // Change l'émotion avec transition animée.
    // Thread-safe : peut être appelé depuis n'importe quelle tâche FreeRTOS.
    void setEmotion(FaceEmotion emotion);

    // Shortcut string : "neutre", "ecoute", "reflexion", "parle", "erreur"
    void setEmotionFromString(const String& name);

    // --- Sync audio (appeler depuis speakerTask à chaque chunk) ---
    // rms : valeur RMS normalisée 0.0 - 1.0 du chunk audio courant
    void setAudioRMS(float rms);

    // --- Boucle principale (appeler toutes les 5-10ms dans displayTask) ---
    void tick(uint32_t delta_ms);

    void _applyEmotion(FaceEmotion emotion, bool animated); // Publique pour callback

    // --- Configuration ---
    FaceConfig config;

    // --- Accès état ---
    FaceEmotion currentEmotion() const { return _emotion; }
    bool isInitialized() const { return _initialized; }

private:
    // ---- Contexte LVGL ----
    TFT_eSPI*         _tft         = nullptr;
    lv_disp_draw_buf_t* _draw_buf  = nullptr;
    lv_color_t*       _buf1        = nullptr;
    lv_disp_drv_t*    _disp_drv    = nullptr;
    lv_disp_t*        _disp        = nullptr;
    bool              _initialized = false;

    // ---- État ----
    FaceEmotion _emotion      = FaceEmotion::NEUTRE;
    FaceEmotion _prevEmotion  = FaceEmotion::NEUTRE;
    float       _audioRMS     = 0.0f;
    float       _smoothRMS    = 0.0f;   // RMS lissé pour la bouche
    uint32_t    _lastBlink    = 0;
    bool        _blinking     = false;
    uint32_t    _blinkStart   = 0;
    uint32_t    _elapsed      = 0;
    uint32_t    _questionPhase= 0;      // Phase animation "?" en REFLEXION

    // ---- Objets LVGL ----
    lv_obj_t* _screen         = nullptr;
    lv_obj_t* _bg             = nullptr;

    // Yeux
    lv_obj_t* _eye_left       = nullptr;
    lv_obj_t* _eye_right      = nullptr;
    lv_obj_t* _pupil_left     = nullptr;
    lv_obj_t* _pupil_right    = nullptr;
    lv_obj_t* _shine_left     = nullptr;
    lv_obj_t* _shine_right    = nullptr;

    // Bouche
    lv_obj_t* _mouth_outer    = nullptr;
    lv_obj_t* _mouth_inner    = nullptr;

    // Joues
    lv_obj_t* _cheek_left     = nullptr;
    lv_obj_t* _cheek_right    = nullptr;

    // Déco émotion-spécifique
    lv_obj_t* _deco_1         = nullptr;  // "?" label / anneau / X gauche
    lv_obj_t* _deco_2         = nullptr;  // point "." / anneau2 / X droit
    lv_obj_t* _scan_ring      = nullptr;  // Anneau ECOUTE

    // ---- Méthodes privées ----
    void _buildBaseObjects();
    void _destroyDecoObjects();
    void _applyNeutre(bool animated);
    void _applyEcoute(bool animated);
    void _applyReflexion(bool animated);
    void _applyParle(bool animated);
    void _applyErreur(bool animated);

    void _setEyeOpenness(float left, float right, bool animated);
    void _setMouthOpen(float openness);
    void _updateBlink(uint32_t now);
    void _updateAudioMouth(uint32_t delta_ms);
    void _updateReflexionAnim(uint32_t delta_ms);
    void _updateScanRing(uint32_t delta_ms);
    void _setObjectColor(lv_obj_t* obj, lv_color_t color, bool animated);

    // Helpers couleur
    static lv_color_t _colorNeutre()    { return lv_color_hex(0x00FFFF); } // Cyan
    static lv_color_t _colorEcoute()    { return lv_color_hex(0x00FF64); } // Vert
    static lv_color_t _colorReflexion() { return lv_color_hex(0xFF32FF); } // Magenta
    static lv_color_t _colorParle()     { return lv_color_hex(0x00FF9F); } // Vert menthe
    static lv_color_t _colorErreur()    { return lv_color_hex(0xFF2020); } // Rouge
    static lv_color_t _colorBg()        { return lv_color_hex(0x000000); } // Fond Noir pur

    static const uint16_t SCR_W = 320;
    static const uint16_t SCR_H = 240;
    static const uint16_t EYE_W = 72;   // Largeur œil de base
    static const uint16_t EYE_H = 72;   // Hauteur œil de base (carré arrondi)
    static const uint16_t EYE_LX = 80;  // X centre œil gauche
    static const uint16_t EYE_RX = 240; // X centre œil droit
    static const uint16_t EYE_Y  = 95;  // Y centre des yeux
    static const uint16_t MOUTH_Y = 175; // Y centre bouche
};

// ============================================================
//  Utilitaire : conversion RMS pour speakerTask
//  Appeler dans speakerTask avant audio->writeSpeaker()
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
