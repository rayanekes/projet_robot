// ============================================================
// ir_remote.h — Télécommande infrarouge pour Robot Assistant
// ============================================================
// Codes calibrés automatiquement par tools/ir_mapper.py
// Télécommande : HX1838    |    Récepteur : IR1838B
// Date         : 2026-05-01 19:47
// ============================================================
#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// ── Configuration matérielle ──────────────────────────────────
#define IR_RECV_PIN 16     // GPIO 16 (Pin utilisé pour le récepteur IR)
#define IR_BUF_SIZE 200

// ── Mode capture (DÉSACTIVÉ — codes calibrés) ────────────────
// Pour recalibrer : décommenter, flasher, relancer ir_mapper.py
// #define IR_CAPTURE_MODE

// ── Codes NEC calibrés ────────────────────────────────────────
namespace IRCodes {
    static const uint32_t SLEEP_WAKE   = 0x00FFA25D;
    static const uint32_t LANG_FR      = 0x00FF30CF;
    static const uint32_t LANG_AR      = 0x00FF18E7;
    static const uint32_t LANG_AUTO    = 0x00FF7A85;
    static const uint32_t PERSO_1      = 0x00FF10EF; // Bouton 4
    static const uint32_t PERSO_2      = 0x00FF38C7; // Bouton 5
    static const uint32_t PERSO_3      = 0x00FF5AA5; // Bouton 6
    static const uint32_t VOL_UP       = 0x00FFA857;
    static const uint32_t VOL_DOWN     = 0x00FFE01F;
    static const uint32_t VOL_MUTE     = 0x00FF906F;
    static const uint32_t TTS_STOP     = 0x00FFC23D;
    static const uint32_t RESET_CONV   = 0x00FF629D;
    static const uint32_t BRIGHT_UP    = 0x00FFB04F;
    static const uint32_t BRIGHT_DN    = 0x00FF9867;
}

// ── Commandes serveur Python ──────────────────────────────────
namespace IRCmd {
    static const char* SLEEP_WAKE   = "cmd:sleep";
    static const char* LANG_FR      = "cmd:lang:fr";
    static const char* LANG_AR      = "cmd:lang:ar";
    static const char* LANG_AUTO    = "cmd:lang:auto";
    static const char* PERSO_1      = "cmd:perso:1";
    static const char* PERSO_2      = "cmd:perso:2";
    static const char* PERSO_3      = "cmd:perso:3";
    static const char* VOL_UP       = "cmd:vol:+10";
    static const char* VOL_DOWN     = "cmd:vol:-10";
    static const char* VOL_MUTE     = "cmd:vol:0";
    static const char* TTS_STOP     = "cmd:stop";
    static const char* RESET_CONV   = "cmd:reset";
    static const char* BRIGHT_UP    = "cmd:bright:+20";
    static const char* BRIGHT_DN    = "cmd:bright:-20";
}

// ── Driver IR ─────────────────────────────────────────────────
class IRRemote {
public:
    IRRemote() : _recv(IR_RECV_PIN, IR_BUF_SIZE) {}

    void begin() {
        pinMode(IR_RECV_PIN, INPUT_PULLUP);
        _recv.setTolerance(25);
        _recv.enableIRIn();
        Serial.printf("[IR] Récepteur prêt sur GPIO %d (INPUT_PULLUP)\n", IR_RECV_PIN);
    }

    const char* poll() {
        if (!_recv.decode(&_results)) return nullptr;
        decode_type_t proto = _results.decode_type;
        uint32_t val = (uint32_t)_results.value;
        _recv.resume();
        if (_results.repeat) return nullptr;
        if (proto == decode_type_t::UNKNOWN) return nullptr;
        return _decode(val);
    }

private:
    IRrecv         _recv;
    decode_results _results;

    const char* _decode(uint32_t code) {
        switch (code) {
            case IRCodes::SLEEP_WAKE: return IRCmd::SLEEP_WAKE;
            case IRCodes::LANG_FR: return IRCmd::LANG_FR;
            case IRCodes::LANG_AR: return IRCmd::LANG_AR;
            case IRCodes::LANG_AUTO: return IRCmd::LANG_AUTO;
            case IRCodes::PERSO_1: return IRCmd::PERSO_1;
            case IRCodes::PERSO_2: return IRCmd::PERSO_2;
            case IRCodes::PERSO_3: return IRCmd::PERSO_3;
            case IRCodes::VOL_UP: return IRCmd::VOL_UP;
            case IRCodes::VOL_DOWN: return IRCmd::VOL_DOWN;
            case IRCodes::VOL_MUTE: return IRCmd::VOL_MUTE;
            case IRCodes::TTS_STOP: return IRCmd::TTS_STOP;
            case IRCodes::RESET_CONV: return IRCmd::RESET_CONV;
            case IRCodes::BRIGHT_UP: return IRCmd::BRIGHT_UP;
            case IRCodes::BRIGHT_DN: return IRCmd::BRIGHT_DN;
            default:
                Serial.printf("[IR] Code inconnu : 0x%08X\n", code);
                return nullptr;
        }
    }
};
