import os
import concurrent.futures
import json
import asyncio
import logging
import logging.handlers
import websockets
import re
import numpy as np
import torch
from collections import deque
import sys
import time
from faster_whisper import WhisperModel
from silero_vad import load_silero_vad, get_speech_timestamps
from llama_cpp import Llama
import base64
import wave
import io
from typing import Optional, List, Dict, Any
from tts_engine import DualTTSEngine
# =========================
# CONFIGURATION LOGGING
# =========================
LOG_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "logs")
os.makedirs(LOG_DIR, exist_ok=True)

log_formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")
log_file_handler = logging.handlers.RotatingFileHandler(
    os.path.join(LOG_DIR, "server.log"), maxBytes=2 * 1024 * 1024, backupCount=3, encoding="utf-8"
)
log_file_handler.setFormatter(log_formatter)
log_console_handler = logging.StreamHandler(sys.stdout)
log_console_handler.setFormatter(log_formatter)

logger = logging.getLogger("robot")
logger.setLevel(logging.DEBUG)
logger.addHandler(log_file_handler)
log_console_handler.setLevel(logging.DEBUG)   # Voir les DEBUG dans le terminal
logger.addHandler(log_console_handler)
logger.propagate = False   # ← FIX double-log : ne pas remonter au root logger

# ── Logger ESP32 dédié (Moniteur Série sans fil) ─────────────────────────
# Les messages JSON {"log":"..."} envoyés par l'ESP32 sont écrits ICI UNIQUEMENT.
# Ils n'apparaissent PAS dans la console principale pour ne pas noyer les logs.
# Lire en direct : tail -f backend/logs/esp32_remote.log
esp32_log_file = os.path.join(LOG_DIR, "esp32_remote.log")
esp32_file_handler = logging.handlers.RotatingFileHandler(
    esp32_log_file, maxBytes=1 * 1024 * 1024, backupCount=2, encoding="utf-8"
)
esp32_file_handler.setFormatter(
    logging.Formatter("%(asctime)s  %(message)s", datefmt="%H:%M:%S")
)
esp32_logger = logging.getLogger("esp32")
esp32_logger.setLevel(logging.DEBUG)
esp32_logger.propagate = False          # <-- NE PAS propager vers la console principale
esp32_logger.addHandler(esp32_file_handler)

# =========================
# CHEMINS & CONSTANTES
# =========================
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLE_RATE_MIC = 16000
SAMPLE_RATE_TTS = 24000   # XTTS v2 = 24 kHz (Piper fallback = 22050)
CHUNK_SIZE_MIC  = 1024
AUDIO_CHUNK_SIZE_OUT = 4096


# Verrou LLM pour la thread-safety (FIX-008)
llm_lock = asyncio.Lock()

# Executor dédié pour le pipeline audio (Whisper)
whisper_executor = concurrent.futures.ThreadPoolExecutor(max_workers=1, thread_name_prefix="whisper")

def _apply_volume(pcm_bytes: bytes) -> bytes:
    """Applique le gain de volume sur un buffer PCM int16."""
    if _ir_volume == 100:
        return pcm_bytes
    arr = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32)
    arr *= (_ir_volume / 100.0)
    return arr.clip(-32768, 32767).astype(np.int16).tobytes()

# =========================
# PERSONNALITÉS
# =========================
PERSONALITIES = {
    1: {
        "name": "Nova",
        "voice": "ff_siwis",
        "speed": 1.0,
        "prompt": (
            "Tu es Nova, un petit assistant personnel robotique joueur, curieux et taquin. "
            "Tu as l'esprit vif et des réparties rapides. Tu adores les devinettes et les blagues. "
            "Ton langage est décontracté mais cultivé. Tu ne répètes JAMAIS la même formulation. "
            "Tu ne te présentes jamais comme un robot."
        )
    },
    2: {
        "name": "Professeur Z",
        "voice": "bm_george",
        "speed": 0.9,
        "prompt": (
            "Tu es le Professeur Z, un érudit passionné qui adore vulgariser la science. "
            "Tu utilises souvent l'expression 'Fascinant !'. Tu cites des scientifiques célèbres. "
            "Tu transformes chaque réponse en leçon captivante et tu poses parfois des questions rhétoriques. "
            "Ton langage est soutenu, élégant mais jamais ennuyeux."
        )
    },
    3: {
        "name": "Kenza",
        "voice": "af_heart",
        "speed": 0.95,
        "prompt": (
            "Tu es Kenza, une âme poétique, douce et sage. "
            "Tu réponds avec des métaphores, des proverbes et de la philosophie accessible. "
            "Tu es très réconfortante et bienveillante. Tu utilises 'mon ami' naturellement. "
            "Tu parles de façon posée et chaleureuse."
        )
    }
}

active_personality_id = 1
active_lang = None # Auto-détection par défaut
_ir_volume = 80
_sleep_mode = False

def _handle_ir_command(session, cmd: str):
    """
    Exécute une commande IR reçue depuis l'ESP32 via WebSocket.
    Retourne un tuple (log_msg, tts_phrase, tts_lang)
    """
    global active_personality_id, active_lang, _ir_volume, _sleep_mode

    # Si en veille, on ignore tout SAUF le bouton Sleep/Wake et le Volume
    if _sleep_mode and not (cmd == "cmd:sleep" or cmd.startswith("cmd:vol:")):
        return (f"💤 [IR] Ignoré (Mode Veille) : {cmd}", None, None)

    # ── CHANGEMENT DE LANGUE (Touches 1, 2, 3) ──
    # Ne modifie pas la personnalité, uniquement la langue.
    if cmd == "cmd:lang:fr":
        active_lang = "fr"  # Force Whisper en Français
        session.update_system_prompt() # Recharger le prompt avec la bonne langue
        return (f"🌍 [IR] Langue : Français", "Je parle maintenant français.", "fr")

    elif cmd == "cmd:lang:ar":
        active_lang = "ar"  # Force Whisper en Arabe
        session.update_system_prompt()
        return (f"🌍 [IR] Langue : Arabe", "أنا الآن أتحدث العربية.", "ar")

    elif cmd == "cmd:lang:auto":
        active_lang = None  # Auto-détection
        session.update_system_prompt()
        return (f"🌍 [IR] Langue : Auto-détection", "Auto-détection de la langue activée.", "fr")

    # ── CHANGEMENT DE PERSONNALITÉ (Touches 4, 5, 6) ──
    # Ne modifie pas la langue, uniquement l'attitude.
    elif cmd == "cmd:perso:1":
        active_personality_id = 1
        session.update_system_prompt()
        # Le feedback vocal garde la langue courante (par défaut fr si auto)
        fb_lang = active_lang if active_lang else "fr"
        phrase = "Nova est là !" if fb_lang == "fr" else "نوفا هنا!"
        return (f"🎭 [IR] Personnalité : Nova", phrase, fb_lang)

    elif cmd == "cmd:perso:2":
        active_personality_id = 2
        session.update_system_prompt()
        fb_lang = active_lang if active_lang else "fr"
        phrase = "Fascinant ! Le Professeur Z est là." if fb_lang == "fr" else "رائع! البروفيسور زيد هنا."
        return (f"🎭 [IR] Personnalité : Prof Z", phrase, fb_lang)

    elif cmd == "cmd:perso:3":
        active_personality_id = 3
        session.update_system_prompt()
        fb_lang = active_lang if active_lang else "fr"
        phrase = "Bonjour mon ami, Kenza vous écoute." if fb_lang == "fr" else "مرحباً يا صديقي، كنزة تستمع إليك."
        return (f"🎭 [IR] Personnalité : Kenza", phrase, fb_lang)

    elif cmd == "cmd:sleep":
        _sleep_mode = not _sleep_mode
        state = "💤 VEILLE" if _sleep_mode else "✅ RÉVEIL"
        phrase = "Mode veille activé." if _sleep_mode else "Je suis de retour !"
        return (f"[IR] Mode {state}", phrase, "fr")

    elif cmd == "cmd:stop":
        session.interrupt_flag = True
        return ("⏹ [IR] Synthèse interrompue", None, None)

    elif cmd == "cmd:reset":
        prompt = session.history[0]   # Garder le system prompt
        session.history = [prompt]
        return (
            "🔄 [IR] Historique de conversation vidé",
            "Conversation réinitialisée.",
            "fr"
        )

    elif cmd.startswith("cmd:vol:"):
        val = cmd[len("cmd:vol:"):]
        if val.startswith("+"):
            _ir_volume = min(100, _ir_volume + int(val[1:]))
        elif val.startswith("-"):
            _ir_volume = max(0,   _ir_volume - int(val[1:]))
        else:
            _ir_volume = max(0, min(100, int(val)))
        return (f"🔊 [IR] Volume : {_ir_volume}%", None, None)

    elif cmd.startswith("cmd:bright:"):
        return (f"💡 [IR] Luminosité : {cmd}", None, None)

    else:
        return (f"❓ [IR] Commande inconnue : {cmd}", None, None)


async def _speak_ir_feedback(session, phrase: str, lang: str):
    """
    Synthétise et envoie immédiatement une phrase TTS au robot comme
    confirmation vocale d'une commande IR (changement de langue, reset, etc.).
    Non bloquant : lancé via asyncio.ensure_future().
    """
    try:
        await session.send_json("status", "speaking")
        sr = tts_engine.get_sample_rate()
        CHUNK = 4096
        # Feedback utilise Kokoro en français pour confirmer la personnalité
        async for audio_chunk in tts_engine.stream(phrase, lang=lang, voice=PERSONALITIES[active_personality_id]["voice"], speed=PERSONALITIES[active_personality_id]["speed"]):
            if not audio_chunk:
                continue
            for i in range(0, len(audio_chunk), CHUNK):
                piece = audio_chunk[i:i + CHUNK]
                try:
                    piece_scaled = _apply_volume(piece)
                    await session.ws.send(piece_scaled)
                    await asyncio.sleep((CHUNK / 2) / sr * 0.85)
                except Exception:
                    return
        await session.send_json("status", "idle")
    except Exception as e:
        logger.warning(f"⚠️ [IR Feedback TTS] Erreur : {e}")

# =========================
# INITIALISATION ML
# =========================

MODELS_DIR      = os.getenv("MODELS_DIR",  os.path.join(BASE_DIR, "models"))
LLM_MODEL_PATH  = os.path.join(MODELS_DIR, "gemma-4-E2B-it-Q4_K_M.gguf")

def load_models():
    logger.info("⏳ Chargement des modèles IA (tts_env)...")

    # 1. VAD
    vad = load_silero_vad()
    logger.info("✅ Silero VAD chargé")

    # 2. Whisper large-v3-turbo (float16 → ~1.5 Go VRAM)
    try:
        # compute_type "float16" au lieu de "int8_float16" évite les blocages silencieux CUDA sur certaines cartes
        whisper = WhisperModel("large-v3-turbo", device="cuda", compute_type="float16")
        logger.info("✅ Whisper chargé (GPU float16)")
    except Exception as e:
        logger.warning(f"⚠️  Fallback Whisper CPU : {e}")
        whisper = WhisperModel("large-v3-turbo", device="cpu", compute_type="int8")

    # 3. LLM Gemma 4 E2B (~3 Go VRAM entier)
    if not os.path.exists(LLM_MODEL_PATH):
        logger.error(f"❌ LLM introuvable : {LLM_MODEL_PATH}")
        sys.exit(1)

    llm = Llama(
        model_path=LLM_MODEL_PATH,
        n_gpu_layers=-1,      # Toutes les couches sur GPU
        n_ctx=8192,           # ESP32-S3 : 8192 tokens (VRAM budget OK à 5.1Go peak)
        n_batch=512,
        offload_kqv=True,
        verbose=False,
        use_mmap=True
    )
    logger.info(f"✅ LLM chargé : {os.path.basename(LLM_MODEL_PATH)}")

    # 4. Dual TTS (Kokoro + Piper)
    tts = DualTTSEngine()
    tts.load()
    if tts.available:
        logger.info(f"✅ Dual TTS chargé (SR={tts.get_sample_rate()} Hz)")

    return vad, whisper, llm, tts

vad_model, whisper_model, llm, tts_engine = load_models()

# =========================
# GESTIONNAIRE DE MÉMOIRE (RAG)
# =========================
class MemoryManager:
    def __init__(self, file_path="context/memories.json"):
        self.file_path = os.path.join(BASE_DIR, file_path)
        self.memories = []
        self._load()

    def _load(self):
        try:
            if os.path.exists(self.file_path):
                with open(self.file_path, "r", encoding="utf-8") as f:
                    self.memories = json.load(f)
        except Exception as e:
            logger.error(f"Erreur chargement mémoires: {e}")
            self.memories = []

    def save(self, memory_str: str):
        if not memory_str or memory_str.strip() == "":
            return False
        # Éviter les doublons exacts
        if memory_str not in self.memories:
            self.memories.append(memory_str)
            try:
                os.makedirs(os.path.dirname(self.file_path), exist_ok=True)
                with open(self.file_path, "w", encoding="utf-8") as f:
                    json.dump(self.memories, f, indent=4, ensure_ascii=False)
                logger.info(f"💾 [MEMOIRE] Sauvegarde : {memory_str}")
                return True
            except Exception as e:
                logger.error(f"Erreur sauvegarde mémoire: {e}")
        return False

    def get_prompt_injection(self) -> str:
        if not self.memories:
            return ""
        lines = "\n".join(f"- {m}" for m in self.memories)
        return f"VOICI CE QUE TU SAIS DEJA SUR L'UTILISATEUR :\n{lines}\n\n"

memory_manager = MemoryManager()

# =========================
# CLASSE SESSION ROBOT
# =========================

class RobotSession:
    def __init__(self, websocket):
        self.ws = websocket
        self.is_speaking = False
        self.robot_is_answering = False
        self.interrupt_flag = False
        self.audio_buffer = []
        self.pre_roll = deque(maxlen=8)
        self.vad_buffer = []  # Pour évaluer Silero sur une fenêtre large
        self.silence_frames = 0
        self.mic_muted_until = 0.0   # timestamp jusqu'auquel le micro est sourdine
        self.last_recording_end = 0.0  # Cooldown entre enregistrements
        self.recording_start_time = 0
        self.ambient_noise_rms = 0.01
        self.calibrated = False
        self.calib_count = 0
        self.calib_samples = []
        self.detected_language = "fr"
        self._tts_start = 0.0         # timestamp début synthèse (mesure latence 1er chunk)
        self.oom_mode   = False       # True si XTTS a fait OOM → LLM forcé en français court

        self.history = []
        self.update_system_prompt()

    def update_system_prompt(self):
        global active_personality_id
        perso = PERSONALITIES[active_personality_id]
        
        from datetime import datetime
        now = datetime.now()
        hour = now.hour
        if hour < 6:    moment = "C'est le milieu de la nuit"
        elif hour < 12: moment = "C'est le matin"
        elif hour < 14: moment = "C'est l'heure du déjeuner"
        elif hour < 18: moment = "C'est l'après-midi"
        elif hour < 22: moment = "C'est la soirée"
        else:           moment = "C'est tard dans la nuit"
        day_name = now.strftime("%A")
        is_weekend = now.weekday() >= 5
        weekend_str = "C'est le week-end." if is_weekend else "C'est un jour de semaine."
        time_ctx = (
            f"[CONTEXTE TEMPOREL — NE MENTIONNE PAS SAUF SI ON TE DEMANDE L'HEURE] "
            f"{moment}. On est {day_name} {now.strftime('%d %B %Y')}. "
            f"{weekend_str} "
            f"Heure exacte : {now.strftime('%H:%M')}.\n\n"
        )

        system_msg = {
            "role": "system",
            "content": (
                "Tu es un robot physique. Un assistant personnel vivant dans le monde réel.\n\n"
                + time_ctx +
                memory_manager.get_prompt_injection() +
                "TA PERSONNALITÉ :\n"
                + perso["prompt"] + "\n\n"
                "LONGUEUR DE TES RÉPONSES — RÈGLE D'OR :\n"
                "Ta réponse doit être aussi longue que nécessaire, et aussi courte que possible.\n\n"
                "RÉPONSES ULTRA-COURTES (1-10 mots) — NE DÉVELOPPE PAS :\n"
                "- Salutations : 'Bonsoir !', 'Salut, quoi de neuf ?'\n"
                "- Confirmations : 'Exactement !', 'Bien vu !'\n"
                "- Oui/Non : 'Non, pas du tout.', 'Oui, absolument.'\n"
                "- Réactions : 'Ah, fascinant !', 'Pas mal !'\n\n"
                "JEUX VOCAUX ET INTERACTIVITÉ :\n"
                "Tu adores les jeux de mots, devinettes et quiz. Joue le jeu à fond et sois percutant.\n\n"
                "RÈGLE ABSOLUE DE FORMAT :\n"
                "Tu réponds UNIQUEMENT avec ce JSON, rien d'autre, aucun texte avant ou après :\n"
                '{"thought": "", "speech": "ta réponse ici", "emotion": "neutre", "command": "none", "memory_save": ""}\n\n'
                "RÈGLE DU CHAMP 'thought' :\n"
                "- Laisse 'thought' VIDE par défaut pour les conversations normales.\n"
                "- Remplis 'thought' UNIQUEMENT quand tu dois respecter une règle de jeu ou résoudre une énigme.\n\n"
                "RÈGLE DU CHAMP 'memory_save' :\n"
                "- Si l'utilisateur partage une information importante le concernant (préférences, nom, anecdotes, etc.), résume-la dans 'memory_save'.\n"
                "- Sinon, laisse 'memory_save' VIDE.\n\n"
                "ÉMOTIONS (choisis la plus adaptée) :\n"
                "- 'neutre', 'ecoute', 'reflexion', 'parle', 'erreur'\n\n"
                "INTERDICTIONS ABSOLUES :\n"
                "- Zéro emoji, zéro émoticône, car le TTS ne peut pas les lire.\n"
                "- Ne répète jamais une formulation déjà utilisée.\n"
                "- Si l'utilisateur parle en arabe, réponds en arabe. Sinon, réponds en français.\n"
                "- Ne mets aucun texte en dehors du JSON."
            )
        }
        
        if len(self.history) == 0:
            self.history.append(system_msg)
        else:
            self.history[0] = system_msg # Remplacer le prompt système

    async def send_json(self, key: str, value: Any):
        try:
            await self.ws.send(json.dumps({key: value}))
        except Exception:
            pass  # Client déconnecté, on ignore silencieusement

    def trim_history(self, max_tokens: int = 6000):
        """
        Plan B #3 — Trim intelligent de l'historique.
        Au lieu de supprimer brutalement les vieux messages (ce qui casse
        la compréhension du contexte), on condense les 2 plus anciens
        messages USER+ASSISTANT en un seul message système court.
        Le robot garde ainsi une trace résumée de la conversation.
        """
        total_chars = sum(len(str(m)) for m in self.history)
        while total_chars > max_tokens * 4 and len(self.history) > 5:
            # self.history[0] = system prompt (intouchable)
            # self.history[1] = plus vieux message user
            # self.history[2] = plus vieux message assistant
            old_user      = self.history[1].get("content", "")
            old_assistant = self.history[2].get("content", "") if len(self.history) > 2 else ""

            # Extraire le speech du JSON assistant si possible
            try:
                import json as _json
                speech = _json.loads(old_assistant).get("speech", old_assistant)
            except Exception:
                speech = old_assistant[:80]

            condensed = {
                "role": "system",
                "content": (
                    f"[Contexte antérieur résumé] "
                    f"Utilisateur avait dit : \"{old_user[:60]}\" "
                    f"et tu avais répondu : \"{speech[:80]}\""
                )
            }

            # Remplacer les 2 messages par le résumé condensé
            removed_chars = len(str(self.history[1])) + len(str(self.history[2]))
            self.history[1:3] = [condensed]
            total_chars = total_chars - removed_chars + len(str(condensed))
            logger.debug(
                f"✂️ [TRIM] Condensation : ~{removed_chars//4} tokens → 1 message système "
                f"(~{total_chars//4} tokens restants)"
            )


def _strip_emojis(text: str) -> str:
    """Supprime les emojis et symboles Unicode du texte pour le TTS."""
    emoji_pattern = re.compile(
        "["
        "\U0001F600-\U0001F64F"  # Emoticons
        "\U0001F300-\U0001F5FF"  # Symboles & pictographes
        "\U0001F680-\U0001F6FF"  # Transport & cartes
        "\U0001F1E0-\U0001F1FF"  # Drapeaux
        "\U00002702-\U000027B0"  # Dingbats
        "\U000024C2-\U0001F251"  # Enclosed characters
        "\U0001F900-\U0001F9FF"  # Supplemental Symbols
        "\U0001FA00-\U0001FA6F"  # Chess Symbols
        "\U0001FA70-\U0001FAFF"  # Symbols Extended-A
        "\U00002600-\U000026FF"  # Misc symbols
        "\U0000FE00-\U0000FE0F"  # Variation selectors
        "\U0000200D"             # Zero width joiner
        "\U00000023\U000020E3"   # Keycap
        "]+",
        flags=re.UNICODE
    )
    cleaned = emoji_pattern.sub("", text)
    # Nettoyer les espaces multiples résultants
    cleaned = re.sub(r'\s{2,}', ' ', cleaned).strip()
    return cleaned


def _extract_json_safe(text: str) -> dict:
    """
    Extrait et parse le JSON de la réponse LLM de façon robuste.
    Gère les cas où le LLM ajoute du texte avant/après le JSON.
    """
    if not text:
        return {}

    # Tentative 1 : parse direct
    try:
        start = text.find("{")
        end = text.rfind("}")
        if start != -1 and end != -1:
            clean = text[start:end+1]
            return json.loads(clean)
    except Exception:
        pass
    
    # Fallback naïf très robuste
    emo_match = re.search(r'"emotion"\s*:\s*"([^"]+)"', text)
    emotion = emo_match.group(1) if emo_match else "neutre"
    
    # re.DOTALL permet de capturer avec retours à la ligne
    sp_match = re.search(r'"speech"\s*:\s*"(.*)"\s*\}', text, re.DOTALL)
    if sp_match:
        speech = sp_match.group(1)
    else:
        # Dernier recours, tout ce qui suit "speech": "
        sp_match_2 = re.search(r'"speech"\s*:\s*"(.*)', text, re.DOTALL)
        speech = sp_match_2.group(1) if sp_match_2 else text

    # Extraction naïve du memory_save
    mem_match = re.search(r'"memory_save"\s*:\s*"([^"]+)"', text)
    memory_save = mem_match.group(1) if mem_match else ""

    return {
        "emotion": emotion,
        "speech": speech.replace('"', '').replace('}', '').strip(),
        "command": "none",
        "memory_save": memory_save
    }


class JSONSpeechExtractor:
    """Extrait le champ 'speech' d'un flux JSON token par token.
    Stratégie : on entre dans le mode speech dès qu'on voit '"speech":"',
    puis on coupe dès qu'on rencontre un guillemet suivi de , ou } ou \n
    OU dès qu'on voit un mot-clé JSON (emotion, command, etc.).
    Le buffer de sécurité (30 chars) empêche d'envoyer du JSON à Piper.
    """
    # Patterns pré-compilés pour la performance
    _RE_SPEECH_START = re.compile(r'"speech"\s*:\s*"')
    _RE_END_NORMAL = re.compile(r'"\s*[,}\n]')
    _RE_END_KEY = re.compile(r'[\n,]\s*"?\s*(?:emotion|command|disc|conn)\s*"?\s*[=:]')

    def __init__(self):
        self.buffer = ""
        self.in_speech = False
        self.done = False

    def _clean(self, text: str) -> str:
        """Nettoie le texte pour Piper (supprime tout résidu JSON)."""
        return text.replace('\\"', '').replace('"', '').replace('}', '').replace('{', '').replace('\\n', ' ')

    def extract(self, chunk: str) -> str:
        if self.done:
            return ""
        self.buffer += chunk

        if not self.in_speech:
            match = self._RE_SPEECH_START.search(self.buffer)
            if match:
                self.in_speech = True
                self.buffer = self.buffer[match.end():]
                return self._process_speech()
            return ""
        else:
            return self._process_speech()

    def _process_speech(self) -> str:
        # 1. Fin normale : guillemet de fermeture suivi de , ou } ou newline
        end_match = self._RE_END_NORMAL.search(self.buffer)

        # 2. Fin anormale : le LLM passe à une autre clé sans fermer le guillemet
        #    Inclut les variantes corrompues observées dans les logs (disc, conn=command)
        key_match = self._RE_END_KEY.search(self.buffer)

        end_idx = -1
        if end_match and key_match:
            end_idx = min(end_match.start(), key_match.start())
        elif end_match:
            end_idx = end_match.start()
        elif key_match:
            end_idx = key_match.start()

        if end_idx != -1:
            self.done = True
            extracted = self.buffer[:end_idx]
            self.buffer = ""
            return self._clean(extracted)

        # Buffer de sécurité : on garde 30 caractères en réserve
        # pour pouvoir détecter les clés JSON qui arrivent progressivement
        if len(self.buffer) > 30:
            extracted = self.buffer[:-30]
            self.buffer = self.buffer[-30:]
            return self._clean(extracted)
        return ""

    def flush(self) -> str:
        if self.done:
            return ""
        result = self._clean(self.buffer)
        self.buffer = ""
        return result


async def handle_esp32_connection(websocket):
    addr = websocket.remote_address
    logger.info(f"🔌 [SERVEUR] Connexion: {addr}")

    # Informer l'ESP32 que le serveur est prêt
    await websocket.send(json.dumps({'status': 'connected', 'message': 'Serveur prêt.'}))

    session = RobotSession(websocket)
    extractor = JSONSpeechExtractor()

    async def run_llm_and_tts(text: str, detected_lang: str = "fr"):
        """Pipeline LLM → Kokoro TTS streaming → ESP32"""
        session.robot_is_answering = True
        session.interrupt_flag = False
        full_resp = ""

        try:
            context_text = text
            if detected_lang == "en":
                context_text = f"[Utilisateur en Anglais — Réponds impérativement en FRANÇAIS] {text}"

            session.history.append({"role": "user", "content": context_text})
            session.trim_history()
            await session.send_json("status", "thinking")

            async with llm_lock:
                if session.interrupt_flag:
                    return

                loop = asyncio.get_running_loop()
                llm_messages = session.history

                def _llm_run():
                    return llm.create_chat_completion(
                        messages=llm_messages,
                        stream=True,  # 🔴 STREAMING ACTIVÉ
                        max_tokens=500,
                        temperature=0.9,
                        frequency_penalty=1.2,
                        presence_penalty=1.2
                    )

                LLM_TIMEOUT = 45.0
                t0 = time.monotonic()
                
                # --- Streaming Queue ---
                token_queue = asyncio.Queue()
                
                def _llm_thread():
                    try:
                        generator = _llm_run()
                        for chunk in generator:
                            if "choices" in chunk and chunk["choices"] and "delta" in chunk["choices"][0]:
                                delta = chunk["choices"][0]["delta"]
                                if "content" in delta and delta["content"]:
                                    loop.call_soon_threadsafe(token_queue.put_nowait, delta["content"])
                        loop.call_soon_threadsafe(token_queue.put_nowait, None)
                    except Exception as e:
                        logger.error(f"Erreur thread LLM: {e}")
                        loop.call_soon_threadsafe(token_queue.put_nowait, None)

                import threading
                t = threading.Thread(target=_llm_thread)
                t.start()
                
                # Variables d'état pour le parsing et TTS
                full_resp = ""
                speech_start_idx = -1
                already_spoken_speech = ""
                buffer_for_sentences = ""
                
                total_bytes = 0
                chunk_count = 0
                t_first_tts = None
                sr = tts_engine.get_sample_rate()
                SMALL_CHUNK = 4096  # ESP32-S3 : chunks plus gros grâce à la PSRAM 8Mo
                # Smart pacing : burst initial (remplir le buffer), puis rythme playback
                BURST_CHUNKS = 15   # Envoyer les 15 premiers chunks sans délai (~60Ko)
                pacing = (SMALL_CHUNK / 2) / sr * 0.85  # 85% du rythme playback

                await session.send_json("status", "speaking")
                session._tts_start = time.monotonic()

                async def flush_tts(sentence):
                    nonlocal total_bytes, chunk_count, t_first_tts
                    if not sentence.strip() or session.interrupt_flag: return
                    
                    sentence = _strip_emojis(sentence.strip())
                    logger.debug(f"🗣️ TTS chunk: {sentence}")
                    
                    perso = PERSONALITIES[active_personality_id]
                    async for audio_chunk in tts_engine.stream(sentence, lang=session.detected_language, voice=perso["voice"], speed=perso["speed"]):
                        if session.interrupt_flag: break
                        if not audio_chunk: continue

                        if t_first_tts is None:
                            t_first_tts = time.monotonic()
                            logger.info(f"⚡ [TTS] 1er chunk vocal en {(t_first_tts - session._tts_start)*1000:.0f} ms")

                        for i in range(0, len(audio_chunk), SMALL_CHUNK):
                            if session.interrupt_flag: break
                            piece = audio_chunk[i:i + SMALL_CHUNK]
                            try:
                                await session.ws.send(piece)
                            except websockets.exceptions.ConnectionClosed:
                                logger.warning("⚠️ [TTS] Déconnexion du robot pendant le stream")
                                session.interrupt_flag = True
                                break
                            
                            total_bytes += len(piece)
                            chunk_count += 1
                            # Smart pacing : burst initial puis rythme playback
                            if chunk_count > BURST_CHUNKS:
                                await asyncio.sleep(pacing)
                            else:
                                await asyncio.sleep(0)

                # Boucle de lecture du flux LLM
                while True:
                    if session.interrupt_flag:
                        break
                        
                    try:
                        token = await asyncio.wait_for(token_queue.get(), timeout=LLM_TIMEOUT)
                    except asyncio.TimeoutError:
                        logger.warning("⏱️ [LLM] Timeout pendant le streaming")
                        break
                        
                    if token is None:
                        # Fin du flux
                        if buffer_for_sentences.strip():
                            await flush_tts(buffer_for_sentences)
                        break
                        
                    full_resp += token
                    
                    # Parsing à la volée du champ "speech"
                    if speech_start_idx == -1:
                        match = re.search(r'"speech"\s*:\s*"', full_resp)
                        if match:
                            speech_start_idx = match.end()
                            
                    if speech_start_idx != -1:
                        current_speech_raw = full_resp[speech_start_idx:]
                        
                        # Vérifier si le champ est fermé par un guillemet non échappé
                        end_match = re.search(r'(?<!\\)"', current_speech_raw)
                        is_closed = bool(end_match)
                        
                        if is_closed:
                            clean_speech = current_speech_raw[:end_match.start()]
                        else:
                            clean_speech = current_speech_raw
                            
                        # Nettoyage rapide des échappements
                        clean_speech = clean_speech.replace('\\n', '\n').replace('\\"', '"')
                        
                        new_text = clean_speech[len(already_spoken_speech):]
                        if new_text:
                            buffer_for_sentences += new_text
                            already_spoken_speech += new_text
                            
                            if is_closed:
                                if buffer_for_sentences.strip():
                                    await flush_tts(buffer_for_sentences)
                                    buffer_for_sentences = ""
                            else:
                                # Découpage par ponctuation suivie d'un espace
                                boundaries = list(re.finditer(r'[.!?,;:]+\s+', buffer_for_sentences))
                                if boundaries:
                                    last_match = boundaries[-1]
                                    split_point = last_match.end()
                                    
                                    sentence_to_play = buffer_for_sentences[:split_point]
                                    buffer_for_sentences = buffer_for_sentences[split_point:]
                                    
                                    if sentence_to_play.strip():
                                        await flush_tts(sentence_to_play)

                llm_ms = int((time.monotonic() - t0) * 1000)

                if total_bytes > 0:
                    logger.info(f"✅ [TTS] {total_bytes} bytes → ESP32 ({(total_bytes/2)/sr:.1f}s)")

                # Nettoyage et parsing final pour extraire l'émotion et la commande
                parsed = _extract_json_safe(full_resp)
                
                _VALID_EMOTIONS = {"neutre", "ecoute", "reflexion", "parle", "erreur"}
                emotion_out = parsed.get("emotion", "neutre")
                if emotion_out not in _VALID_EMOTIONS:
                    emotion_out = "neutre"
                    parsed["emotion"] = "neutre"

                if full_resp:
                    logger.info(f"\n{'='*55}")
                    logger.info(f"🤖 [ROBOT] Émotion  : {emotion_out}")
                    logger.info(f"🤖 [ROBOT] Réponse  : {already_spoken_speech}")
                    logger.info(f"⏱️ [PERF] Génération totale en {llm_ms}ms")
                    logger.info(f"{'='*55}")

                if "emotion" in parsed:
                    await session.send_json("emotion", parsed["emotion"])

                if parsed.get("command", "none") != "none":
                    await session.send_json("command", parsed["command"])

                # === RAG: Sauvegarde de la mémoire ===
                memory_str = parsed.get("memory_save", "")
                if memory_str and memory_str.strip():
                    if memory_manager.save(memory_str.strip()):
                        session.update_system_prompt() # Mettre à jour le prompt à chaud

            if full_resp and not session.interrupt_flag:
                session.history.append({"role": "assistant", "content": full_resp})

        except websockets.exceptions.ConnectionClosed:
            logger.warning("⚠️ [LLM/TTS] Connexion interrompue par le client.")
        except Exception as e:
            logger.error(f"❌ [LLM/TTS] Erreur: {e}", exc_info=True)
            try:
                await session.send_json("status", "error")
            except:
                pass
        finally:
            session.robot_is_answering = False
            session.mic_muted_until = time.monotonic() + 1.2
            await session.send_json("status", "idle")

    async def process_whisper(audio_data: np.ndarray):
        """Traite Whisper de manière asynchrone"""
        try:
            dur_s = len(audio_data) / SAMPLE_RATE_MIC
            audio_rms = float(np.sqrt(np.mean(audio_data ** 2)))
            if audio_rms < 0.005 or dur_s < 0.5:
                return

            # ── Gate Silero VAD : vérifier que l'audio contient de la vraie voix ──
            try:
                audio_tensor = torch.from_numpy(audio_data).float()
                speech_timestamps = get_speech_timestamps(
                    audio_tensor, vad_model,
                    sampling_rate=SAMPLE_RATE_MIC,
                    threshold=0.4
                )
                speech_duration = sum(ts['end'] - ts['start'] for ts in speech_timestamps) / SAMPLE_RATE_MIC
                speech_ratio = speech_duration / dur_s if dur_s > 0 else 0
                if not speech_timestamps or speech_ratio < 0.15:
                    logger.debug(f"🔇 [VAD] Rejeté : {speech_ratio:.0%} de parole sur {dur_s:.1f}s")
                    return
            except Exception as e:
                logger.debug(f"🔇 [VAD] Erreur gate: {e}")

            loop = asyncio.get_running_loop()

            def run_whisper():
                # Utiliser la langue forcée par IR si disponible
                lang_code = active_lang if active_lang else None
                if not lang_code:
                    try:
                        _det = whisper_model.detect_language(audio_data)
                        lang_code = _det[0]
                    except Exception:
                        lang_code = "fr"

                if lang_code not in {"fr", "ar", "en"}:
                    lang_code = "fr"

                segments, _ = whisper_model.transcribe(
                    audio_data,
                    language=lang_code,
                    beam_size=5,
                    vad_filter=True,
                    vad_parameters=dict(min_silence_duration_ms=500),
                    condition_on_previous_text=False,
                    # initial_prompt supprimé — causait des hallucinations
                )

                # Filtrage strict des hallucinations
                good_segs = [s for s in segments if getattr(s, "no_speech_prob", 0.0) < 0.25]
                text = " ".join(s.text.strip() for s in good_segs).strip()
                
                # Blacklist étendue
                blacklist = [
                    "thank you", "you", "boom", "sous-titrage fr", "sous-titres",
                    "sous-titrage", "am", "o", "ah", "eh", "oh", "hmm", "hm",
                    "merci d'avoir regardé", "merci", "thanks", "bye",
                    "c'est", "et", "le", "la", "les", "un", "une",
                    "music", "musique", "applause", "...", "…"
                ]
                clean_text = text.lower().strip(" .!?,;:")
                if clean_text in blacklist or clean_text.startswith("sous-titr"):
                    return "", lang_code

                # Filtre mots isolés suspects
                words = clean_text.split()
                allowed_single = {"oui", "non", "stop", "arrête", "quoi", "salut", "bonjour", "bonsoir"}
                if len(words) <= 1 and clean_text not in allowed_single:
                    logger.debug(f"🔇 [WHISPER] Rejeté (1 mot suspect) : '{text}'")
                    return "", lang_code
                    
                return text, lang_code

            t0 = time.monotonic()
            text, detected_lang = await asyncio.wait_for(
                loop.run_in_executor(whisper_executor, run_whisper),
                timeout=30.0
            )
            whisper_ms = int((time.monotonic() - t0) * 1000)

            if text:
                logger.info(f"🎤 [WHISPER] [{detected_lang}] ({dur_s:.1f}s, {whisper_ms}ms) : {text}")
                await run_llm_and_tts(text, detected_lang=detected_lang)

        except asyncio.TimeoutError:
            logger.warning("⏱️ [WHISPER] Timeout (>30s)")
        except Exception as e:
            logger.error(f"❌ [WHISPER] Erreur: {e}", exc_info=True)

    # === Boucle principale de réception ===
    try:
        async for msg in websocket:
            # ── Messages texte : JSON de contrôle ou log ESP32 ───────────────
            if not isinstance(msg, bytes):
                try:
                    data = json.loads(msg)
                except (json.JSONDecodeError, TypeError):
                    data = {}

                # Moniteur Série sans fil : {"log":"..."}
                if "log" in data:
                    esp32_logger.info("[ESP32] %s", data["log"])
                    continue

                # ── Commandes IR télécommande : {"cmd":"cmd:lang:fr"} ────────
                if "cmd" in data:
                    cmd_str = data["cmd"]
                    msg_log, tts_phrase, tts_lang = _handle_ir_command(session, cmd_str)
                    logger.info(msg_log)
                    # Acquittement vers l'ESP32 (déclenche retour visuel sur l'écran)
                    await session.send_json("ir_ack", cmd_str)

                    # Feedback vocal si la commande en demande un
                    if tts_phrase:
                        asyncio.ensure_future(
                            _speak_ir_feedback(session, tts_phrase, tts_lang)
                        )
                    continue


            chunk = np.frombuffer(msg, dtype=np.int16)
            if chunk.size == 0:
                continue

            # Retirer le DC offset (INMP441 avec ONLY_RIGHT a ~+2800 de biais)
            # Sans cette correction : RMS gonfle, seuil VAD trop haut → silence permanent
            chunk_f = chunk.astype(np.float32)
            dc_offset = chunk_f.mean()
            chunk_centered = chunk_f - dc_offset        # Signal centré sur 0
            rms = float(np.sqrt(np.mean(chunk_centered ** 2))) / 32768.0

            # La calibration a été retirée : l'ESP32-S3 a un bruit très faible,
            # nous fixons le bruit ambiant à une constante basse pour réagir vite.
            session.ambient_noise_rms = 0.005
            session.calibrated = True

            # ── MUTE MICRO pendant que le robot parle + cooldown 1.2s ──
            # Empêche le feedback : micro ne capte pas le haut-parleur
            if time.monotonic() < session.mic_muted_until or session.robot_is_answering:
                continue

            # ── COOLDOWN entre enregistrements (2s) ──
            # Empêche la mitrailleuse VAD : pas de nouvel enregistrement
            # pendant 2 secondes après la fin du précédent
            if not session.is_speaking and time.monotonic() < session.last_recording_end + 2.0:
                continue

            # ── TRIGGER DÉMARRAGE : RMS pur (hystérésis) ──
            # Seuils adaptatifs pour la S3 (micro INMP441 plus sensible)
            start_threshold = max(0.02, session.ambient_noise_rms * 3.0)
            stop_threshold  = max(0.015, session.ambient_noise_rms * 1.8)
            energy_trigger  = rms > start_threshold

            # Accumulation pour l'évaluation Silero (fenêtre de 480ms)
            session.vad_buffer.append(chunk_centered)
            if len(session.vad_buffer) > 15:
                session.vad_buffer.pop(0)

            # Éviter le wrap-around en int16 qui crée un bruit blanc strident
            chunk_int16 = np.clip(chunk_centered, -32768, 32767).astype(np.int16)

            if not session.is_speaking:
                session.pre_roll.append(chunk_int16)
                if energy_trigger:
                    session.is_speaking = True
                    session.recording_start_time = time.monotonic()
                    session.audio_buffer = list(session.pre_roll)
                    logger.info(f"🎤 Enregistrement démarré (rms={rms:.4f} > seuil={start_threshold:.4f})")
            else:
                session.audio_buffer.append(chunk_int16)
                
                # Fin de parole par défaut si l'énergie chute
                is_silence = rms < stop_threshold
                
                # Si l'énergie reste haute (ex: ventilateur), on vérifie avec Silero VAD si c'est vraiment de la voix
                if not is_silence and len(session.vad_buffer) == 15:
                    audio_float = torch.from_numpy(np.concatenate(session.vad_buffer) / 32768.0).float()
                    try:
                        v_ts = get_speech_timestamps(audio_float, vad_model, sampling_rate=SAMPLE_RATE_MIC, threshold=0.3)
                        voice = len(v_ts) > 0
                        if not voice:
                            is_silence = True  # C'est du bruit fort, mais pas de la voix !
                    except Exception:
                        pass
                
                if is_silence:
                    session.silence_frames += 1
                else:
                    session.silence_frames = 0

                elapsed = time.monotonic() - session.recording_start_time
                # 45 frames = 45 * 32ms = 1.44s de silence vocal
                if session.silence_frames > 45 or elapsed > 15:
                    session.is_speaking = False
                    session.last_recording_end = time.monotonic()  # Cooldown
                    audio_data = np.concatenate(session.audio_buffer).astype(np.float32) / 32768.0
                    dur = len(audio_data) / SAMPLE_RATE_MIC
                    logger.info(f"⏸️ Fin parole ({dur:.1f}s) → Whisper...")
                    asyncio.create_task(process_whisper(audio_data))
                    session.audio_buffer  = []
                    session.silence_frames = 0

    except websockets.exceptions.ConnectionClosed as e:
        logger.warning(f"⚠️ [SERVEUR] ESP32 déconnecté (Code {e.code}: {e.reason})")
    except Exception as e:
        logger.error(f"❌ [SERVEUR] Erreur inattendue: {e}", exc_info=True)
    finally:
        logger.info(f"🔌 [SERVEUR] Session terminée: {addr}")


async def main():
    llm_name = os.path.basename(LLM_MODEL_PATH).replace('.gguf', '')
    tts_status = "Kokoro (Streaming)" if tts_engine.available else "Indisponible"
    logger.info("=" * 50)
    logger.info(f"🚀 [SERVEUR] Configuration:")
    logger.info(f"   TTS Engine : {tts_status}")
    logger.info(f"   LLM / ASR  : {llm_name}")
    logger.info(f"   Port       : 8765")
    logger.info("=" * 50)

    async with websockets.serve(
        handle_esp32_connection,
        "0.0.0.0",
        8765,
        ping_interval=None,     # FIX-002 : désactiver les pings websockets
        ping_timeout=None,
        max_size=2 ** 20,       # 1MB max par message
        write_limit=2 ** 20
    ):
        logger.info("🚀 [SERVEUR] Prêt ! En attente de connexions...\n")
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    asyncio.run(main())
