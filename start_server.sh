#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════
#  start_server.sh — Robot Assistant IA v2.0
#  Architecture Dual-MCU : ESP32-S3 (Master) + ESP32 V1 (Audio)
#
#  Usage :
#    ./start_server.sh                      ← profil "default"
#    ./start_server.sh --voice rayane       ← profil "rayane"
#    ./start_server.sh --list-voices        ← afficher les profils
#    ./start_server.sh --add-voice NOM LANG WAV ← ajouter une voix
# ═══════════════════════════════════════════════════════════════
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
MODELS_DIR="$BACKEND_DIR/models"
VOICES_DIR="$BACKEND_DIR/voices"
PIPER_DIR="$BACKEND_DIR/piper"
PYTHON="$HOME/tts_env/bin/python"
PIPER_BIN_DEFAULT="$HOME/tts_env/bin/piper"

# ═══════════════════════════════════════════════════════════════
#  PALETTE DE COULEURS — Neon Soul
# ═══════════════════════════════════════════════════════════════
R='\033[0m'          # Reset
B='\033[1m'          # Bold
DIM='\033[2m'        # Dim
UL='\033[4m'         # Underline

# Couleurs vives
RED='\033[38;5;203m'
GRN='\033[38;5;114m'
YLW='\033[38;5;221m'
BLU='\033[38;5;75m'
CYN='\033[38;5;87m'
MGN='\033[38;5;183m'
ORG='\033[38;5;209m'
PNK='\033[38;5;218m'
WHT='\033[38;5;255m'
GRY='\033[38;5;242m'

# Backgrounds
BG_OK='\033[48;5;22m'
BG_ERR='\033[48;5;52m'
BG_WARN='\033[48;5;58m'

# ═══════════════════════════════════════════════════════════════
#  FONCTIONS UTILITAIRES
# ═══════════════════════════════════════════════════════════════

# Afficher un spinner pendant une commande
spin() {
    local msg="$1"
    local frames=('⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏')
    local i=0
    while true; do
        printf "\r  ${CYN}${frames[$i]}${R} ${GRY}${msg}${R}  "
        i=$(( (i + 1) % ${#frames[@]} ))
        sleep 0.08
    done
}

# Afficher une barre de progression (sans newline finale — le ok/warn/fail l'écrase)
progress_bar() {
    local current=$1
    local total=$2
    local width=30
    local pct=$((current * 100 / total))
    local filled=$((current * width / total))
    local empty=$((width - filled))

    local bar=""
    for ((i=0; i<filled; i++)); do bar+="█"; done
    for ((i=0; i<empty; i++)); do bar+="░"; done

    printf "\r  ${GRY}[${CYN}%s${GRY}]${R} ${WHT}%s%%${R}  " "$bar" "$pct"
}

# Résultat OK
ok() {
    printf "\r  ${GRN}✓${R} ${WHT}%s${R}" "$1"
    [ -n "$2" ] && printf " ${GRY}— %s${R}" "$2"
    echo ""
}

# Résultat Warning
warn() {
    printf "\r  ${YLW}⚠${R} ${YLW}%s${R}" "$1"
    [ -n "$2" ] && printf " ${GRY}— %s${R}" "$2"
    echo ""
}

# Résultat Erreur
fail() {
    printf "\r  ${RED}✗${R} ${RED}%s${R}" "$1"
    [ -n "$2" ] && printf " ${GRY}— %s${R}" "$2"
    echo ""
}

# Séparateur élégant
sep() {
    echo -e "  ${GRY}─────────────────────────────────────────────${R}"
}

# Taille lisible
human_size() {
    local bytes=$1
    if [ "$bytes" -ge 1073741824 ]; then
        echo "$(echo "scale=1; $bytes/1073741824" | bc) Go"
    elif [ "$bytes" -ge 1048576 ]; then
        echo "$(echo "scale=0; $bytes/1048576" | bc) Mo"
    else
        echo "$(echo "scale=0; $bytes/1024" | bc) Ko"
    fi
}

# ═══════════════════════════════════════════════════════════════
#  PARSE ARGUMENTS
# ═══════════════════════════════════════════════════════════════
VOICE_PROFILE="default"
ADD_VOICE_NAME=""
ADD_VOICE_LANG=""
ADD_VOICE_WAV=""
LIST_VOICES=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --voice|-v)
            VOICE_PROFILE="$2"; shift 2 ;;
        --list-voices|-l)
            LIST_VOICES=true; shift ;;
        --add-voice|-a)
            ADD_VOICE_NAME="$2"
            ADD_VOICE_LANG="$3"
            ADD_VOICE_WAV="$4"
            shift 4 ;;
        *)
            echo -e "${RED}✗ Argument inconnu : $1${R}"
            echo "Usage: ./start_server.sh [--voice NOM] [--list-voices] [--add-voice NOM LANG WAV]"
            exit 1 ;;
    esac
done

# ═══════════════════════════════════════════════════════════════
#  HEADER ANIMÉ
# ═══════════════════════════════════════════════════════════════
clear 2>/dev/null || true
echo ""
echo ""

# Animation d'apparition du logo
LOGO_LINES=(
    "${CYN}    ┌─────────────────────────────────────────────────────┐${R}"
    "${CYN}    │${R}                                                     ${CYN}│${R}"
    "${CYN}    │${R}   ${B}${MGN}◉${R}  ${B}${WHT}R O B O T   A S S I S T A N T   I A${R}        ${CYN}│${R}"
    "${CYN}    │${R}                                                     ${CYN}│${R}"
    "${CYN}    │${R}   ${GRY}Architecture${R}  ${BLU}Dual-MCU${R} ${GRY}(S3 + V1)${R}              ${CYN}│${R}"
    "${CYN}    │${R}   ${GRY}LLM${R}           ${ORG}Gemma 4 E2B${R} ${GRY}(Q4_K_M)${R}           ${CYN}│${R}"
    "${CYN}    │${R}   ${GRY}TTS${R}           ${PNK}Kokoro${R} ${GRY}+ Piper (Streaming)${R}      ${CYN}│${R}"
    "${CYN}    │${R}   ${GRY}ASR${R}           ${GRN}Whisper large-v3-turbo${R}          ${CYN}│${R}"
    "${CYN}    │${R}                                                     ${CYN}│${R}"
    "${CYN}    └─────────────────────────────────────────────────────┘${R}"
)

for line in "${LOGO_LINES[@]}"; do
    echo -e "$line"
    sleep 0.04
done

echo ""
echo -e "  ${GRY}$(date '+%A %d %B %Y — %H:%M')${R}"
echo ""

# ═══════════════════════════════════════════════════════════════
#  COMMANDE : --list-voices
# ═══════════════════════════════════════════════════════════════
if [ "$LIST_VOICES" = true ]; then
    echo -e "  ${B}${MGN}🎙️  Profils de voix disponibles :${R}"
    sep
    "$PYTHON" -c "from tts_engine import list_voices; print(list_voices())" 2>/dev/null \
        || echo -e "  ${YLW}(Aucun profil trouvé)${R}"
    echo ""
    echo -e "  ${CYN}Pour ajouter une voix :${R}"
    echo -e "  ${GRY}$${R} ${WHT}./start_server.sh --add-voice NOM fr /chemin/voix_fr.wav${R}"
    echo -e "  ${GRY}$${R} ${WHT}./start_server.sh --add-voice NOM ar /chemin/voix_ar.wav${R}"
    exit 0
fi

# ═══════════════════════════════════════════════════════════════
#  COMMANDE : --add-voice
# ═══════════════════════════════════════════════════════════════
if [ -n "$ADD_VOICE_NAME" ]; then
    echo -e "  ${MGN}🎙️  Ajout de voix${R} : profil=${B}${ADD_VOICE_NAME}${R} lang=${B}${ADD_VOICE_LANG}${R}"

    if [ ! -f "$ADD_VOICE_WAV" ]; then
        fail "Fichier WAV introuvable" "$ADD_VOICE_WAV"
        exit 1
    fi

    if [[ "$ADD_VOICE_LANG" != "fr" && "$ADD_VOICE_LANG" != "ar" ]]; then
        fail "Langue invalide" "'${ADD_VOICE_LANG}' (doit être 'fr' ou 'ar')"
        exit 1
    fi

    mkdir -p "$VOICES_DIR"

    DEST_WAV="$VOICES_DIR/${ADD_VOICE_NAME}_${ADD_VOICE_LANG}.wav"
    cp "$ADD_VOICE_WAV" "$DEST_WAV"
    ok "WAV copié" "$DEST_WAV"

    cd "$BACKEND_DIR"
    "$PYTHON" - <<EOF
import sys
sys.path.insert(0, "src")
from tts_engine import save_profile, load_profiles, VoiceProfile
import os

profiles = load_profiles()
existing = profiles.get("${ADD_VOICE_NAME}", VoiceProfile(name="${ADD_VOICE_NAME}"))

if "${ADD_VOICE_LANG}" == "fr":
    save_profile("${ADD_VOICE_NAME}",
                 fr_wav="$DEST_WAV",
                 ar_wav=existing.ar_wav)
else:
    save_profile("${ADD_VOICE_NAME}",
                 fr_wav=existing.fr_wav,
                 ar_wav="$DEST_WAV")

print(f"✅ Profil '${ADD_VOICE_NAME}' mis à jour dans profiles.json")
EOF

    echo ""
    ok "Voix ajoutée !" "Utiliser avec : ./start_server.sh --voice ${ADD_VOICE_NAME}"
    exit 0
fi

# ═══════════════════════════════════════════════════════════════
#  VÉRIFICATIONS — Pipeline de démarrage
# ═══════════════════════════════════════════════════════════════
echo -e "  ${B}${BLU}▸ Vérifications système${R}"
sep
STEP=0
TOTAL=7

# --- [1/7] Python (tts_env) ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
if [ ! -x "$PYTHON" ]; then
    echo ""
    fail "Python (tts_env) introuvable" "$PYTHON"
    echo -e "  ${GRY}→ python3.11 -m venv ~/tts_env && pip install -r requirements.txt${R}"
    exit 1
fi
PY_VER=$($PYTHON --version 2>&1 | awk '{print $2}')
ok "Python" "v${PY_VER} (tts_env)"

# --- [2/7] LLM (Gemma) ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
LLM_FILE="$MODELS_DIR/gemma-4-E2B-it-Q4_K_M.gguf"
if [ ! -f "$LLM_FILE" ]; then
    echo ""
    fail "LLM introuvable" "$(basename $LLM_FILE)"
    exit 1
fi
LLM_SIZE=$(human_size $(stat -c%s "$LLM_FILE" 2>/dev/null || stat -f%z "$LLM_FILE" 2>/dev/null))
ok "LLM Gemma 4 E2B" "${LLM_SIZE}"

# --- [3/7] TTS Kokoro (ONNX) ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
KOKORO_ONNX="$MODELS_DIR/kokoro-v1.0.onnx"
KOKORO_VOICES="$MODELS_DIR/voices-v1.0.bin"
if [ -f "$KOKORO_ONNX" ] && [ -f "$KOKORO_VOICES" ]; then
    KOKORO_SIZE=$(human_size $(stat -c%s "$KOKORO_ONNX" 2>/dev/null || stat -f%z "$KOKORO_ONNX" 2>/dev/null))
    ok "TTS Kokoro" "${KOKORO_SIZE} + voices.bin"
else
    fail "TTS Kokoro ONNX introuvable" "kokoro-v1.0.onnx / voices-v1.0.bin"
    exit 1
fi

# --- [4/7] TTS Piper (Fallback Arabe) ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
PIPER_AR="$MODELS_DIR/piper/ar_JO-kareem-low.onnx"
if [ -f "$PIPER_AR" ]; then
    ok "TTS Piper (AR)" "ar_JO-kareem-low.onnx"
else
    warn "Piper arabe absent" "La voix arabe sera indisponible"
fi

# --- [5/7] Piper (Fallback CPU — Français) ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
export PIPER_BIN="${PIPER_BIN:-$PIPER_BIN_DEFAULT}"
PIPER_MODEL_FR="$PIPER_DIR/fr_FR-siwis-medium.onnx"
if [ -x "$PIPER_BIN" ] && [ -f "$PIPER_MODEL_FR" ]; then
    export PIPER_MODEL="$PIPER_MODEL_FR"
    ok "Piper fallback (FR)" "fr_FR-siwis-medium.onnx"
else
    warn "Piper FR non trouvé" "Fallback CPU désactivé"
fi

# --- [6/7] Whisper (ASR) ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
WHISPER_CACHE="$MODELS_DIR/whisper"
if [ -d "$WHISPER_CACHE" ] && [ "$(find "$WHISPER_CACHE" -type f -size +100M 2>/dev/null | head -1)" ]; then
    WHISPER_SIZE=$(du -sh "$WHISPER_CACHE" 2>/dev/null | cut -f1)
    ok "Whisper large-v3-turbo" "${WHISPER_SIZE} (cache)"
else
    warn "Whisper cache vide" "Sera téléchargé au premier lancement (~1.6 Go)"
fi

# --- [7/7] GPU & VRAM ---
STEP=$((STEP + 1))
progress_bar $STEP $TOTAL
if command -v nvidia-smi &>/dev/null; then
    GPU_NAME=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)
    VRAM_TOTAL=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null | head -1)
    VRAM_FREE=$(nvidia-smi --query-gpu=memory.free --format=csv,noheader,nounits 2>/dev/null | head -1)
    VRAM_PCT=$((VRAM_FREE * 100 / VRAM_TOTAL))
    ok "GPU ${GPU_NAME}" "${VRAM_FREE}/${VRAM_TOTAL} MiB libre (${VRAM_PCT}%)"
    if [ "$VRAM_FREE" -lt 4500 ]; then
        warn "VRAM faible" "Budget estimé : ~4.5 Go (Gemma + Whisper + Kokoro)"
    fi
else
    warn "Aucun GPU NVIDIA détecté" "Mode CPU uniquement"
fi

echo ""

# ═══════════════════════════════════════════════════════════════
#  PROFIL DE VOIX
# ═══════════════════════════════════════════════════════════════
echo -e "  ${B}${MGN}▸ Profil de voix : ${WHT}${VOICE_PROFILE}${R}"
sep
mkdir -p "$VOICES_DIR"

cd "$BACKEND_DIR"
PROFILE_STATUS=$("$PYTHON" - <<EOF 2>/dev/null
import sys, os
sys.path.insert(0, "src")
try:
    from tts_engine import load_profiles
    profiles = load_profiles()
    if "${VOICE_PROFILE}" not in profiles:
        print("NOT_FOUND")
    else:
        p = profiles["${VOICE_PROFILE}"]
        fr = "✓" if p.fr_exists else "manquant"
        ar = "✓" if p.ar_exists else "manquant"
        print(f"OK|{fr}|{ar}")
except Exception as e:
    print(f"ERROR: {e}")
EOF
)

if [[ "$PROFILE_STATUS" == "NOT_FOUND" ]]; then
    warn "Profil '${VOICE_PROFILE}' non trouvé" "Synthèse générique (pas de clonage)"
    echo -e "  ${GRY}→ ./start_server.sh --add-voice ${VOICE_PROFILE} fr /chemin/voix.wav${R}"
elif [[ "$PROFILE_STATUS" == ERROR* ]]; then
    warn "Erreur profil" "$PROFILE_STATUS"
else
    IFS='|' read -ra PARTS <<< "$PROFILE_STATUS"
    if [[ "${PARTS[1]}" == "✓" ]]; then
        ok "Voix FR" "Chargée"
    else
        warn "Voix FR" "Absente"
    fi
    if [[ "${PARTS[2]}" == "✓" ]]; then
        ok "Voix AR" "Chargée"
    else
        warn "Voix AR" "Absente"
    fi
fi

echo ""

# ═══════════════════════════════════════════════════════════════
#  AUTO-CONVERSION INBOX
# ═══════════════════════════════════════════════════════════════
INBOX_DIR="$VOICES_DIR/inbox"
mkdir -p "$INBOX_DIR"
INBOX_COUNT=$(find "$INBOX_DIR" -maxdepth 1 \
    \( -iname "*.m4a" -o -iname "*.ogg" -o -iname "*.opus" \
       -o -iname "*.mp3" -o -iname "*.flac" -o -iname "*.aac" \) \
    2>/dev/null | wc -l)

if [ "$INBOX_COUNT" -gt 0 ]; then
    echo -e "  ${B}${ORG}▸ Inbox : ${INBOX_COUNT} fichier(s) à convertir${R}"
    sep
    cd "$SCRIPT_DIR"
    bash convert_voice.sh 2>/dev/null && ok "Conversion terminée" || warn "Conversion partielle"
    echo ""
fi

# ═══════════════════════════════════════════════════════════════
#  LANCEMENT DU SERVEUR
# ═══════════════════════════════════════════════════════════════
export PYTORCH_CUDA_ALLOC_CONF="expandable_segments:True,max_split_size_mb:256"
export VOICE_PROFILE

echo -e "  ${B}${BLU}▸ Démarrage du serveur${R}"
sep
echo ""
echo -e "  ${B}${CYN}🚀 Lancement${R} ${GRY}|${R} ${WHT}Profil : ${VOICE_PROFILE}${R} ${GRY}|${R} ${GRY}Ctrl+C pour stopper${R}"
echo ""
echo -e "  ${GRY}╭──────────────────────────────────────────────╮${R}"
echo -e "  ${GRY}│${R}  ${DIM}Déposer des voix dans :${R}                      ${GRY}│${R}"
echo -e "  ${GRY}│${R}  ${CYN}backend/voices/inbox/rayane_fr.m4a${R}           ${GRY}│${R}"
echo -e "  ${GRY}│${R}  ${DIM}→ Converties automatiquement au prochain boot${R} ${GRY}│${R}"
echo -e "  ${GRY}╰──────────────────────────────────────────────╯${R}"
echo ""

cd "$BACKEND_DIR"

# ─── Plan B : Auto-redémarrage en cas de crash ────────────────
RESTART_COUNT=0
while true; do
    if [ "$RESTART_COUNT" -gt 0 ]; then
        echo ""
        echo -e "  ${RED}${B}✗ Crash détecté${R} ${GRY}(code: $EXIT_CODE)${R} ${GRY}— redémarrage #${RESTART_COUNT} dans 3s...${R}"
        sleep 3
        echo -e "  ${CYN}↻ Relancement...${R}"
        echo ""
    fi

    "$PYTHON" src/server.py
    EXIT_CODE=$?
    RESTART_COUNT=$((RESTART_COUNT + 1))

    # Arrêt volontaire (Ctrl+C → 130, SIGTERM → 143, exit 0)
    if [ "$EXIT_CODE" -eq 130 ] || [ "$EXIT_CODE" -eq 143 ] || [ "$EXIT_CODE" -eq 0 ]; then
        echo ""
        echo -e "  ${CYN}${B}👋 Arrêt propre${R} ${GRY}(code $EXIT_CODE)${R}"
        echo -e "  ${GRY}À bientôt !${R}"
        echo ""
        break
    fi
done
