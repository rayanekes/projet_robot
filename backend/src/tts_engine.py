import os
import logging
import numpy as np
import asyncio
import json
from scipy.signal import resample_poly

from kokoro_onnx import Kokoro
from piper import PiperVoice

logger = logging.getLogger("robot")

MODELS_DIR = "/home/rayane/projet_robot/backend/models"
PROFILES_FILE = "/home/rayane/projet_robot/backend/voices/profiles.json"

class VoiceProfile:
    def __init__(self, name, fr_wav=None, ar_wav=None):
        self.name = name
        self.fr_wav = fr_wav
        self.ar_wav = ar_wav
    
    @property
    def fr_exists(self):
        return self.fr_wav and os.path.exists(self.fr_wav)
    
    @property
    def ar_exists(self):
        return self.ar_wav and os.path.exists(self.ar_wav)

def load_profiles():
    if not os.path.exists(PROFILES_FILE):
        return {}
    try:
        with open(PROFILES_FILE, "r") as f:
            data = json.load(f)
            return {name: VoiceProfile(name, **p) for name, p in data.items()}
    except Exception as e:
        logger.error(f"Erreur chargement profiles.json : {e}")
        return {}

def save_profile(name, fr_wav=None, ar_wav=None):
    profiles = load_profiles()
    p = {"fr_wav": fr_wav, "ar_wav": ar_wav}
    profiles[name] = p
    # Convert profiles to dict for JSON
    data = {name: {"fr_wav": p.fr_wav, "ar_wav": p.ar_wav} for name, p in profiles.items()}
    with open(PROFILES_FILE, "w") as f:
        json.dump(data, f, indent=4)

class KokoroEngine:
    def __init__(self):
        self.model = None
        self.available = False
        self._sample_rate = 24000

    def load(self):
        try:
            logger.info("🎙️ Chargement de Kokoro-ONNX (CPU)...")
            self.model = Kokoro(
                os.path.join(MODELS_DIR, "kokoro-v1.0.onnx"),
                os.path.join(MODELS_DIR, "voices-v1.0.bin")
            )
            self.available = True
            logger.info("✅ Kokoro TTS chargé.")
        except Exception as e:
            logger.error(f"❌ Erreur chargement Kokoro : {e}")

    async def stream(self, text, voice="ff_siwis", speed=1.0):
        if not self.available:
            return
        try:
            stream_gen = self.model.create_stream(
                text,
                voice=voice,
                speed=speed,
                lang="fr-fr"
            )
            async for samples, sample_rate in stream_gen:
                audio_pcm = (samples * 32767).clip(-32768, 32767).astype(np.int16)
                yield audio_pcm.tobytes()
        except Exception as e:
            logger.error(f"Erreur Kokoro stream : {e}")


class PiperEngine:
    def __init__(self):
        self.voice = None
        self.available = False
        self._sample_rate = 16000

    def load(self):
        try:
            logger.info("🎙️ Chargement de Piper (Arabe)...")
            model_path = os.path.join(MODELS_DIR, "piper", "ar_JO-kareem-low.onnx")
            if os.path.exists(model_path):
                self.voice = PiperVoice.load(model_path)
                self.available = True
                logger.info("✅ Piper TTS chargé (Kareem).")
            else:
                logger.error(f"❌ Modèle Piper introuvable : {model_path}")
        except Exception as e:
            logger.error(f"❌ Erreur chargement Piper : {e}")

    async def stream(self, text):
        if not self.available:
            return
        try:
            def _piper_gen():
                return list(self.voice.synthesize_stream_raw(text))
            
            chunks = await asyncio.to_thread(_piper_gen)
            
            for chunk_bytes in chunks:
                if not chunk_bytes: continue
                arr = np.frombuffer(chunk_bytes, dtype=np.int16).astype(np.float32)
                resampled = resample_poly(arr, 3, 2)
                audio_pcm = resampled.clip(-32768, 32767).astype(np.int16)
                yield audio_pcm.tobytes()
                await asyncio.sleep(0)
                
        except Exception as e:
            logger.error(f"Erreur Piper stream : {e}")


class DualTTSEngine:
    def __init__(self):
        self.kokoro = KokoroEngine()
        self.piper = PiperEngine()
    
    def load(self):
        self.kokoro.load()
        self.piper.load()
    
    @property
    def available(self):
        return self.kokoro.available or self.piper.available

    def get_sample_rate(self):
        return 24000

    async def stream(self, text, lang="fr", voice="ff_siwis", speed=1.0):
        if lang == "ar" and self.piper.available:
            async for chunk in self.piper.stream(text):
                yield chunk
        elif self.kokoro.available:
            async for chunk in self.kokoro.stream(text, voice=voice, speed=speed):
                yield chunk
