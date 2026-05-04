#!/usr/bin/env bash
# =============================================================
# convert_voice.sh — Gestion des voix pour XTTS v2
#
# MODE 1 — Dépôt automatique (recommandé) :
#   1. Dépose ton enregistrement dans :
#      backend/voices/inbox/rayane_fr.m4a   (Pixel 8)
#      backend/voices/inbox/rayane_ar.m4a
#   2. Lance : ./convert_voice.sh
#   → Convertit tout et met à jour profiles.json
#
# MODE 2 — Conversion manuelle :
#   ./convert_voice.sh FILE PROFIL LANGUE
#   ./convert_voice.sh ~/Downloads/rec.m4a rayane fr
#
# Formats acceptés : M4A, OGG, OPUS, MP3, FLAC, WAV (n'importe quel sample rate)
#
# Convention de nommage (OBLIGATOIRE pour le mode auto) :
#   PROFIL_fr.ext  →  profil "PROFIL", langue française
#   PROFIL_ar.ext  →  profil "PROFIL", langue arabe
#   Exemples : rayane_fr.m4a, rayane_ar.ogg, robot_fr.opus
# =============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$SCRIPT_DIR/backend"
VOICES_DIR="$BACKEND_DIR/voices"
INBOX_DIR="$VOICES_DIR/inbox"
PROCESSED_DIR="$INBOX_DIR/processed"
PYTHON="$HOME/tts_env/bin/python"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# =============================================================
# FONCTION : convertir un fichier → WAV 22050 Hz mono
# =============================================================
convert_file() {
    local INPUT="$1"
    local PROFILE="$2"
    local LANG="$3"

    if [[ "$LANG" != "fr" && "$LANG" != "ar" ]]; then
        echo -e "${RED}❌ Langue invalide : '$LANG' (doit être 'fr' ou 'ar')${NC}"
        return 1
    fi

    OUTPUT="$VOICES_DIR/${PROFILE}_${LANG}.wav"

    # Sauvegarde si une version précédente existe
    if [ -f "$OUTPUT" ]; then
        BACKUP="${OUTPUT%.wav}_backup_$(date +%Y%m%d_%H%M%S).wav"
        mv "$OUTPUT" "$BACKUP"
        echo -e "${YELLOW}   ⚠️  Sauvegarde : $(basename $BACKUP)${NC}"
    fi

    echo -e "   ⏳ Conversion → ${PROFILE}_${LANG}.wav  (22050 Hz, mono, 16 bits)"

    ffmpeg -y \
        -i "$INPUT" \
        -ar 22050 \
        -ac 1 \
        -c:a pcm_s16le \
        -af "highpass=f=80,lowpass=f=12000,loudnorm=I=-16:TP=-1.5:LRA=11" \
        "$OUTPUT" \
        -loglevel error 2>&1 || {
            echo -e "${RED}   ❌ ffmpeg a échoué pour : $(basename $INPUT)${NC}"
            return 1
        }

    if [ ! -f "$OUTPUT" ]; then
        echo -e "${RED}   ❌ Fichier de sortie absent${NC}"
        return 1
    fi

    DURATION=$(ffprobe -v quiet -show_entries format=duration \
        -of csv=p=0 "$OUTPUT" 2>/dev/null | cut -d. -f1)
    echo -e "   ${GREEN}✅ ${PROFILE}_${LANG}.wav  (${DURATION}s, 22050 Hz)${NC}"

    if [ "${DURATION:-0}" -lt 6 ]; then
        echo -e "   ${YELLOW}⚠️  Durée courte (${DURATION}s) — recommandé : 15-30s${NC}"
    fi

    # Invalider le cache des embeddings XTTS (le WAV a changé)
    CACHE_FILE="$VOICES_DIR/.cache/${PROFILE}_${LANG}.pt"
    if [ -f "$CACHE_FILE" ]; then
        rm "$CACHE_FILE"
        echo -e "   ${CYAN}🔄 Cache embedding invalidé (sera recalculé au prochain démarrage)${NC}"
    fi

    # Mettre à jour profiles.json
    cd "$BACKEND_DIR"
    "$PYTHON" - <<EOF 2>/dev/null && true
import sys
sys.path.insert(0, "src")
from tts_engine import save_profile, load_profiles, VoiceProfile
profiles = load_profiles()
existing = profiles.get("${PROFILE}", VoiceProfile(name="${PROFILE}"))
if "${LANG}" == "fr":
    save_profile("${PROFILE}", fr_wav="$OUTPUT", ar_wav=existing.ar_wav)
else:
    save_profile("${PROFILE}", fr_wav=existing.fr_wav, ar_wav="$OUTPUT")
EOF

    return 0
}

# =============================================================
# VÉRIFICATIONS COMMUNES
# =============================================================
if ! command -v ffmpeg &>/dev/null; then
    echo -e "${RED}❌ ffmpeg non installé → sudo apt install ffmpeg${NC}"
    exit 1
fi
mkdir -p "$VOICES_DIR" "$INBOX_DIR" "$PROCESSED_DIR"

# =============================================================
# MODE 2 : conversion manuelle  ./convert_voice.sh FILE PROFIL LANGUE
# =============================================================
if [ -n "$1" ] && [ -n "$2" ] && [ -n "$3" ]; then
    INPUT="$1"
    PROFILE="$2"
    LANG="$3"

    echo -e "${CYAN}${BOLD}🎙️  Conversion manuelle${NC}"
    echo -e "   Source  : $(basename $INPUT)"
    echo -e "   Profil  : $PROFILE  |  Langue : $LANG"
    echo ""

    if [ ! -f "$INPUT" ]; then
        echo -e "${RED}❌ Fichier introuvable : $INPUT${NC}"
        exit 1
    fi

    convert_file "$INPUT" "$PROFILE" "$LANG"
    echo ""
    echo -e "${GREEN}✅ Terminé. Lancer le serveur avec :${NC}"
    echo -e "   ${BOLD}./start_server.sh --voice $PROFILE${NC}"
    exit 0
fi

# =============================================================
# MODE 1 : auto-scan de inbox/
# =============================================================
echo -e "${CYAN}${BOLD}📥 Scan automatique de inbox/${NC}"
echo -e "${CYAN}   Dossier : $INBOX_DIR${NC}"
echo ""

# Afficher le guide si inbox est vide
FOUND=0
for EXT in m4a ogg opus mp3 flac wav aac; do
    COUNT=$(find "$INBOX_DIR" -maxdepth 1 -iname "*.${EXT}" 2>/dev/null | wc -l)
    FOUND=$((FOUND + COUNT))
done

if [ "$FOUND" -eq 0 ]; then
    echo -e "${YELLOW}📂 Aucun fichier dans inbox/${NC}"
    echo ""
    echo -e "${BOLD}${CYAN}Comment enregistrer avec ton Pixel 8 :${NC}"
    echo ""
    echo -e "  ${BOLD}Option A — Application Google Recorder (intégrée)${NC}"
    echo -e "    1. Ouvre 'Enregistreur' sur le Pixel 8"
    echo -e "    2. Enregistre ${BOLD}15-30 secondes${NC} de parole naturelle (pas de bruit)"
    echo -e "    3. Exporte en M4A depuis l'app"
    echo ""
    echo -e "  ${BOLD}Option B — Via ADB (USB)${NC}"
    echo -e "    adb pull /sdcard/Download/recording.m4a ~/Downloads/"
    echo ""
    echo -e "  ${BOLD}Option C — Google Drive${NC}"
    echo -e "    Partage le fichier depuis le Pixel → télécharge sur le PC"
    echo ""
    echo -e "${BOLD}${CYAN}Nommage des fichiers (OBLIGATOIRE) :${NC}"
    echo -e "  ${BOLD}PROFIL_LANGUE.ext${NC}"
    echo -e "  Exemples :"
    echo -e "    rayane_fr.m4a   → profil 'rayane', voix française"
    echo -e "    rayane_ar.m4a   → profil 'rayane', voix arabe"
    echo -e "    robot_fr.ogg    → profil 'robot', voix française"
    echo ""
    echo -e "${CYAN}Dossier à remplir :${NC}"
    echo -e "  ${BOLD}$INBOX_DIR/${NC}"
    echo ""
    echo -e "${CYAN}Ensuite relance :${NC}  ${BOLD}./convert_voice.sh${NC}"
    exit 0
fi

echo -e "${BOLD}$FOUND fichier(s) trouvé(s) à convertir${NC}"
echo ""

CONVERTED=0
FAILED=0

# Traiter chaque fichier audio dans inbox/
for EXT in m4a ogg opus mp3 flac wav aac M4A OGG OPUS MP3 FLAC WAV AAC; do
    while IFS= read -r -d '' FILE; do
        BASENAME=$(basename "$FILE")
        NAME="${BASENAME%.*}"  # sans extension

        # Extraire PROFIL et LANGUE depuis le nom : PROFIL_LANGUE.ext
        if [[ "$NAME" =~ ^(.+)_(fr|ar)$ ]]; then
            PROFILE="${BASH_REMATCH[1]}"
            LANG="${BASH_REMATCH[2]}"
        else
            echo -e "${YELLOW}⚠️  Nom invalide : '$BASENAME'${NC}"
            echo -e "${YELLOW}   → Renommer en PROFIL_fr.ext ou PROFIL_ar.ext${NC}"
            echo -e "${YELLOW}   → Ex: rayane_fr.m4a${NC}"
            continue
        fi

        echo -e "${BOLD}▸ $BASENAME${NC}  →  profil='$PROFILE'  langue='$LANG'"
        if convert_file "$FILE" "$PROFILE" "$LANG"; then
            # Déplacer vers processed/
            mv "$FILE" "$PROCESSED_DIR/"
            echo -e "   ${CYAN}📦 Déplacé vers inbox/processed/${NC}"
            CONVERTED=$((CONVERTED + 1))
        else
            FAILED=$((FAILED + 1))
        fi
        echo ""
    done < <(find "$INBOX_DIR" -maxdepth 1 -iname "*.${EXT}" -print0 2>/dev/null)
done

# =============================================================
# RÉSUMÉ
# =============================================================
echo -e "${BOLD}─────────────────────────────────────────────${NC}"
echo -e "${GREEN}✅ Convertis : $CONVERTED${NC}"
[ "$FAILED" -gt 0 ] && echo -e "${RED}❌ Échoués  : $FAILED${NC}"
echo ""

if [ "$CONVERTED" -gt 0 ]; then
    echo -e "${CYAN}Profils disponibles :${NC}"
    cd "$BACKEND_DIR"
    "$PYTHON" -c "
import sys; sys.path.insert(0, 'src')
from tts_engine import load_profiles
profiles = load_profiles()
for name, p in profiles.items():
    fr = '✅' if p.fr_exists else '❌'
    ar = '✅' if p.ar_exists else '❌'
    print(f'  [{name}]  FR: {fr}  AR: {ar}')
" 2>/dev/null || true

    echo ""
    echo -e "${CYAN}Lancer le serveur avec un profil :${NC}"
    echo -e "  ${BOLD}./start_server.sh --voice NOM_PROFIL${NC}"
    echo -e "  ${BOLD}./start_server.sh --list-voices${NC}"
fi
