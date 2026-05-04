#!/usr/bin/env python3
# =============================================================================
# tools/ir_mapper.py — Cartographie de télécommande IR HX1838
# =============================================================================
# Usage :
#   1. Flasher le firmware avec IR_CAPTURE_MODE actif
#   2. Brancher l'ESP32 en USB
#   3. python3 tools/ir_mapper.py
#   4. Suivre les instructions : appuyer 3× sur chaque bouton demandé
#   5. Le script génère firmware/include/ir_remote.h avec les vrais codes
# =============================================================================

import sys
import os
import re
import json
import time

# ── Dépendance pyserial ───────────────────────────────────────────────────────
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("📦 Installation de pyserial...")
    os.system(f"{sys.executable} -m pip install pyserial -q")
    import serial
    import serial.tools.list_ports

# ── Chemins ───────────────────────────────────────────────────────────────────
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR  = os.path.dirname(SCRIPT_DIR)
IR_HEADER    = os.path.join(PROJECT_DIR, "firmware", "include", "ir_remote.h")
MAPPING_JSON = os.path.join(SCRIPT_DIR, "ir_mapping.json")

# ── Boutons à cartographier ──────────────────────────────────────────────────
BUTTONS = [
    {"key": "SLEEP_WAKE", "cmd": "cmd:sleep",      "label": "🔴 POWER / SLEEP",        "hint": "Le bouton CH- ou Power en haut à gauche"},
    {"key": "LANG_FR",    "cmd": "cmd:lang:fr",     "label": "🇫🇷 LANGUE FRANÇAISE (1)", "hint": "Bouton '1'"},
    {"key": "LANG_AR",    "cmd": "cmd:lang:ar",     "label": "🇲🇦 LANGUE ARABE (2)",     "hint": "Bouton '2'"},
    {"key": "LANG_AUTO",  "cmd": "cmd:lang:auto",   "label": "🔄 AUTO-DÉTECTION (3)",   "hint": "Bouton '3'", "optional": True},
    {"key": "VOL_UP",     "cmd": "cmd:vol:+10",     "label": "🔊 VOLUME +",             "hint": "Bouton '+'"},
    {"key": "VOL_DOWN",   "cmd": "cmd:vol:-10",     "label": "🔉 VOLUME -",             "hint": "Bouton '-'"},
    {"key": "VOL_MUTE",   "cmd": "cmd:vol:0",       "label": "🔇 MUTE",                 "hint": "Bouton 'EQ'"},
    {"key": "TTS_STOP",   "cmd": "cmd:stop",        "label": "⏹ STOP TTS",              "hint": "Bouton '0' ou '>||'"},
    {"key": "RESET_CONV", "cmd": "cmd:reset",       "label": "🔄 RESET CONVERSATION",   "hint": "Bouton 'CH' ou '200+'"},
    {"key": "BRIGHT_UP",  "cmd": "cmd:bright:+20",  "label": "💡 LUMINOSITÉ +",         "hint": "Bouton 'CH+' ou ▲", "optional": True},
    {"key": "BRIGHT_DN",  "cmd": "cmd:bright:-20",  "label": "🔅 LUMINOSITÉ -",         "hint": "Bouton '|<<' ou ▼", "optional": True},
]

CONFIRMATIONS_REQUIRED = 3


# ── Détection port Série ─────────────────────────────────────────────────────
def choose_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("❌ Aucun port Série détecté. Branche l'ESP32 en USB et relance.")
        sys.exit(1)

    # Filtrer : ne montrer que les ports USB réels (ignorer ttyS*)
    usb_ports = [p for p in ports if "USB" in p.device.upper() or "ACM" in p.device.upper()]
    all_ports = usb_ports if usb_ports else ports

    print("\n📡 Ports Série disponibles :")
    for i, p in enumerate(all_ports):
        print(f"  [{i}] {p.device} — {p.description}")

    if len(all_ports) == 1:
        print(f"\n  → Détecté automatiquement : {all_ports[0].device}")
        ans = input("  Utiliser ce port ? [O/n] : ").strip().lower()
        if ans in ("", "o", "y", "oui", "yes"):
            return all_ports[0].device

    idx = input(f"\nNuméro du port (0-{len(all_ports)-1}) : ").strip()
    return all_ports[int(idx)].device


# ── Parse une ligne de capture IR ────────────────────────────────────────────
def parse_ir_line(line):
    """
    Parse les lignes du firmware en mode IR_CAPTURE_MODE.
    Format attendu : [IR_CAP] proto=NEC val=0xFFA25D cmd=0x5D addr=0x00 bits=32
    Retourne (code_hex, protocol) ou (None, None).
    """
    if "[IR_CAP]" not in line:
        return None, None

    # Signal non décodé (bruit, pas de pull-up, etc.)
    if "UNKNOWN" in line:
        return None, "UNKNOWN"

    m_val = re.search(r"val=(0x[0-9A-Fa-f]+)", line)
    m_proto = re.search(r"proto=(\w+)", line)

    if m_val:
        code = int(m_val.group(1), 16)
        proto = m_proto.group(1) if m_proto else "?"
        return code, proto

    return None, None


# ── Lire un code IR depuis le port Série ─────────────────────────────────────
def read_ir_code(ser, timeout=20.0):
    """
    Attend un code IR valide depuis le port Série.
    Retourne (code, protocol) ou (None, None) si timeout.
    """
    deadline = time.time() + timeout
    unknown_count = 0
    ser.reset_input_buffer()

    while time.time() < deadline:
        try:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
        except Exception:
            continue

        if not line:
            continue

        code, proto = parse_ir_line(line)

        # Signal non décodé
        if proto == "UNKNOWN":
            unknown_count += 1
            if unknown_count >= 3:
                print("\n  ⚠️  Signal reçu mais non décodé (UNKNOWN).")
                print("     Vérifie :")
                print("     • Le récepteur est bien sur GPIO 16 (PAS 34)")
                print("     • VCC = 3.3V, GND = GND, OUT = GPIO 16")
                print("     • La télécommande est pointée vers le récepteur")
                unknown_count = 0
            continue

        if code is not None:
            return code, proto

    return None, None


# ── Cartographie d'un bouton ──────────────────────────────────────────────────
def map_button(ser, button):
    label    = button["label"]
    hint     = button["hint"]
    optional = button.get("optional", False)

    print(f"\n{'═'*60}")
    print(f"  ➤  {label}")
    print(f"     {hint}")
    if optional:
        print("     (optionnel — Ctrl+C pour passer)")
    print(f"{'═'*60}")
    print(f"  Appuie {CONFIRMATIONS_REQUIRED}× sur le bouton choisi.\n")

    collected = []

    while len(collected) < CONFIRMATIONS_REQUIRED:
        remaining = CONFIRMATIONS_REQUIRED - len(collected)
        print(f"  [{len(collected)}/{CONFIRMATIONS_REQUIRED}] "
              f"En attente... ({remaining} restante(s))", end="\r", flush=True)

        code, proto = read_ir_code(ser, timeout=30.0)

        if code is None:
            if optional:
                ans = input(f"\n  Timeout. Passer ce bouton ? [O/n] : ").strip().lower()
                if ans in ("", "o", "y", "oui", "yes"):
                    return None
            else:
                print("\n  ⚠️  Timeout — réessaie (vérifie le câblage).")
            continue

        # Cohérence : vérifier que c'est le même bouton
        if collected and code != collected[0]:
            print(f"\n  ❌ Code différent ! 0x{code:08X} ≠ 0x{collected[0]:08X}")
            print(f"     → Appuie sur le MÊME bouton. On recommence.")
            collected.clear()
            continue

        collected.append(code)
        print(f"  ✅ 0x{code:08X} ({proto})  [{len(collected)}/{CONFIRMATIONS_REQUIRED}]"
              + "          ")  # espaces pour effacer la ligne précédente

    final_code = collected[0]
    print(f"\n  🎯 '{label}' → 0x{final_code:08X}  ✔")
    return final_code


# ── Génération du ir_remote.h ─────────────────────────────────────────────────
def generate_header(mapping: dict):
    from datetime import datetime

    codes_lines = []
    cmds_lines  = []
    cases_lines = []

    for btn in BUTTONS:
        key  = btn["key"]
        cmd  = btn["cmd"]
        code = mapping.get(key)
        if code is None:
            codes_lines.append(f"    // {key} : non assigné")
            continue
        codes_lines.append(f"    static const uint32_t {key:<12} = 0x{code:08X};")
        cmds_lines.append(f'    static const char* {key:<12} = "{cmd}";')
        cases_lines.append(f"            case IRCodes::{key}: return IRCmd::{key};")

    return f'''\
// ============================================================
// ir_remote.h — Télécommande infrarouge pour Robot Assistant
// ============================================================
// Codes calibrés automatiquement par tools/ir_mapper.py
// Télécommande : HX1838    |    Récepteur : IR1838B
// Date         : {datetime.now().strftime("%Y-%m-%d %H:%M")}
// ============================================================
#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// ── Configuration matérielle ──────────────────────────────────
#define IR_RECV_PIN 16
#define IR_BUF_SIZE 200

// ── Mode capture (DÉSACTIVÉ — codes calibrés) ────────────────
// Pour recalibrer : décommenter, flasher, relancer ir_mapper.py
// #define IR_CAPTURE_MODE

// ── Codes NEC calibrés ────────────────────────────────────────
namespace IRCodes {{
{chr(10).join(codes_lines)}
}}

// ── Commandes serveur Python ──────────────────────────────────
namespace IRCmd {{
{chr(10).join(cmds_lines)}
}}

// ── Driver IR ─────────────────────────────────────────────────
class IRRemote {{
public:
    IRRemote() : _recv(IR_RECV_PIN, IR_BUF_SIZE) {{}}

    void begin() {{
        pinMode(IR_RECV_PIN, INPUT_PULLUP);
        _recv.setTolerance(25);
        _recv.enableIRIn();
        Serial.printf("[IR] Récepteur prêt sur GPIO %d (INPUT_PULLUP)\\n", IR_RECV_PIN);
    }}

    const char* poll() {{
        if (!_recv.decode(&_results)) return nullptr;
        decode_type_t proto = _results.decode_type;
        uint32_t val = (uint32_t)_results.value;
        _recv.resume();
        if (_results.repeat) return nullptr;
        if (proto == decode_type_t::UNKNOWN) return nullptr;
        return _decode(val);
    }}

private:
    IRrecv         _recv;
    decode_results _results;

    const char* _decode(uint32_t code) {{
        switch (code) {{
{chr(10).join(cases_lines)}
            default:
                Serial.printf("[IR] Code inconnu : 0x%08X\\n", code);
                return nullptr;
        }}
    }}
}};
'''


# ── Programme principal ───────────────────────────────────────────────────────
def main():
    print("\n" + "═"*60)
    print("  🎯 Cartographie Télécommande IR — HX1838 + IR1838B")
    print("═"*60)
    print(f"""
  Pour chaque bouton :
  • Appuie {CONFIRMATIONS_REQUIRED}× sur le MÊME bouton pour confirmer
  • Si les codes ne correspondent pas, on recommence

  Pré-requis :
  ✓ Récepteur IR1838B branché : VCC=3.3V, GND, OUT=GPIO 16
  ✓ Firmware flashé avec IR_CAPTURE_MODE actif
  ✓ ESP32 connecté en USB
""")

    port = choose_port()
    print(f"\n📡 Connexion sur {port} @ 115200...")

    try:
        ser = serial.Serial(port, 115200, timeout=1.0)
    except serial.SerialException as e:
        print(f"❌ Impossible d'ouvrir {port} : {e}")
        sys.exit(1)

    time.sleep(2)
    ser.reset_input_buffer()

    # ── Test rapide : vérifier que le récepteur fonctionne ──────────
    print("✅ Connecté.\n")
    print("  🧪 TEST RAPIDE : appuie sur n'importe quel bouton de la télécommande...")
    code, proto = read_ir_code(ser, timeout=15.0)
    if code is None:
        print("\n  ❌ Aucun signal IR reçu en 15 secondes.")
        print("     Vérifie :")
        print("     1. Le récepteur est bien sur GPIO 16 (pas 34 !)")
        print("     2. Le câblage : VCC=3.3V, GND=GND, OUT=GPIO 16")
        print("     3. La télécommande a des piles")
        ans = input("\n  Continuer quand même ? [o/N] : ").strip().lower()
        if ans not in ("o", "y", "oui", "yes"):
            ser.close()
            sys.exit(1)
    else:
        print(f"  ✅ Signal reçu ! 0x{code:08X} ({proto})")
        print("     Le récepteur fonctionne. Début de la cartographie...\n")

    # ── Cartographie ───────────────────────────────────────────────
    mapping = {}

    for i, button in enumerate(BUTTONS, 1):
        print(f"\n  Bouton {i}/{len(BUTTONS)}")
        try:
            code = map_button(ser, button)
        except KeyboardInterrupt:
            if button.get("optional"):
                print(f"\n  ⏭ '{button['label']}' ignoré.")
                code = None
            else:
                print("\n\n⚠️  Interrompu. Progression sauvegardée.")
                break

        if code is not None:
            # Vérifier les doublons
            for prev_key, prev_code in mapping.items():
                if prev_code == code:
                    print(f"\n  ⚠️  DOUBLON ! Même code que '{prev_key}'. Réessaie.")
                    code = None
                    break
            if code is not None:
                mapping[button["key"]] = code

    ser.close()

    if not mapping:
        print("\n❌ Aucun bouton cartographié. Rien n'a été sauvegardé.")
        sys.exit(1)

    # ── Résumé ─────────────────────────────────────────────────────
    print("\n\n" + "═"*60)
    print("  ✅ Cartographie terminée !")
    print("═"*60)
    for btn in BUTTONS:
        code = mapping.get(btn["key"])
        status = f"0x{code:08X}" if code else "(non assigné)"
        print(f"  {btn['label']:<35}  →  {status}")

    # ── Sauvegarde ─────────────────────────────────────────────────
    os.makedirs(os.path.dirname(MAPPING_JSON), exist_ok=True)
    with open(MAPPING_JSON, "w", encoding="utf-8") as f:
        json.dump({k: f"0x{v:08X}" for k, v in mapping.items()}, f, indent=2)
    print(f"\n💾 Backup JSON : {MAPPING_JSON}")

    header_content = generate_header(mapping)
    with open(IR_HEADER, "w", encoding="utf-8") as f:
        f.write(header_content)
    print(f"✅ ir_remote.h  : {IR_HEADER}")
    print("   (IR_CAPTURE_MODE désactivé)\n")

    print("═"*60)
    print("  📌 Prochaines étapes :")
    print("  1. Reflasher : cd firmware && pio run --target upload")
    print("  2. Lancer    : ./start_server.sh --voice rayane")
    print("  3. Tester    : bouton 1 → 🇫🇷 français forcé !")
    print("═"*60 + "\n")


if __name__ == "__main__":
    main()
