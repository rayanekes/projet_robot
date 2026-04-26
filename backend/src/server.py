import os
import json
import asyncio
import websockets
import re
import numpy as np
import torch
import scipy.io.wavfile as wav
from collections import deque
import sys
import time
from faster_whisper import WhisperModel
from silero_vad import load_silero_vad, get_speech_timestamps
from llama_cpp import Llama
from typing import Optional, List, Dict, Any

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS_DIR = os.path.expanduser("~/projet_robot/models")
PIPER_BIN = "/home/rayane/tts_env/bin/piper"
PIPER_MODEL = os.path.join(BASE_DIR, "piper", "fr_FR-siwis-medium.onnx")

SAMPLE_RATE_MIC = 16000
SAMPLE_RATE_TTS = 22050
CHUNK_SIZE_MIC = 1024
AUDIO_CHUNK_SIZE_OUT = 4096

# Verrou LLM pour la thread-safety (FIX-008)
llm_lock = asyncio.Lock()

# =========================
# INITIALISATION ML
# =========================

LLM_MODEL_PATH = os.path.join(MODELS_DIR, "qwen2.5-7b-instruct-q4_k_m.gguf")
WHISPER_MODEL = "medium"
WHISPER_DEVICE = "cuda"

def load_models():
    print("⏳ Chargement des modèles IA...")
    
    # 1. VAD
    vad = load_silero_vad()
    print("✅ Silero VAD chargé")
    
    # 2. Whisper
    try:
        whisper = WhisperModel(WHISPER_MODEL, device=WHISPER_DEVICE, compute_type="int8_float16")
        print("✅ Whisper chargé (GPU - Optimisé VRAM)")
    except:
        whisper = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
        print("⚠️ Whisper chargé (CPU)")

    # 3. LLaMA (Calcul dynamique VRAM - OPT-007)
    n_layers = -1
    try:
        free_vram, total_vram = torch.cuda.mem_get_info()
        MODEL_VRAM_MB = 4500
        SAFETY_MARGIN_MB = 400
        if free_vram < (MODEL_VRAM_MB + SAFETY_MARGIN_MB) * 1024 * 1024:
            n_layers = 20
            print(f"⚠️ VRAM faible, offload partiel: {n_layers} layers")
        print(f"✅ LLaMA config: n_gpu_layers={n_layers} (VRAM libre: {free_vram/1024**2:.0f}MB)")
    except:
        n_layers = 0

    llm = Llama(model_path=LLM_MODEL_PATH, n_gpu_layers=n_layers, n_ctx=4096, verbose=False, use_mmap=True)
    print("✅ LLaMA chargé")
    
    return vad, whisper, llm

vad_model, whisper, llm = load_models()

# =========================
# CLASSE SESSION ROBOT
# =========================

class RobotSession:
    def __init__(self, websocket):
        self.ws = websocket
        self.history = [{"role": "system", "content": self._get_system_prompt()}]
        self.is_speaking = False
        self.robot_is_answering = False
        self.interrupt_flag = False
        self.audio_buffer = []
        self.pre_roll = deque(maxlen=8)
        self.silence_frames = 0
        self.recording_start_time = 0
        self.ambient_noise_rms = 0.01
        self.calibrated = False
        self.calib_count = 0
        self.calib_samples = [] # OPT-003

    def _get_system_prompt(self) -> str:
        return (
            "Tu es un robot assistant d'ingénierie. Réponds TOUJOURS en français pur, sans chichi. "
            "Si l'utilisateur dit 'je veux écouter [titre]', 'mets [titre]', ou mentionne vouloir de la musique, "
            "tu DOIS utiliser \"command\": \"play_song\" et extraire exactement le titre dans \"title\": \"[titre]\". "
            "Par exemple, si on te dit 'je veux écouter L Morphine Skit', ton JSON sera : "
            "{\"speech\": \"Je lance Skit de L Morphine.\", \"emotion\": \"joie\", \"command\": \"play_song\", \"title\": \"morphine\"}. "
            "Si l'utilisateur dit 'ouvre spotify' ou 'ouvre l'interface mp3', utilise \"command\": \"start_mp3\". "
            "Si l'utilisateur dit 'ferme spotify', 'ferme la musique', 'arrête la musique', utilise \"command\": \"stop_mp3\". "
            "Si l'utilisateur dit 'pause', 'mets en pause la musique', utilise \"command\": \"pause_mp3\". "
            "Si l'utilisateur dit 'reprend la', 'continue la musique', utilise \"command\": \"resume_mp3\". "
            "Sinon, utilise \"command\": \"none\" et choisis une émotion (neutre, ecoute, reflexion, parle, erreur). "
            "Réponds STRICTEMENT au format JSON."
        )

    async def send_json(self, key: str, value: Any):
        try:
            await self.ws.send(json.dumps({key: value}))
        except: pass

    def trim_history(self, max_tokens: int = 3000): # OPT-004
        total_chars = sum(len(str(m)) for m in self.history)
        while total_chars > max_tokens * 4 and len(self.history) > 3:
            removed = self.history.pop(1)
            total_chars -= len(str(removed))
            print(f"✂️ [TRIM] ~{total_chars//4} tokens restants")

class JSONSpeechExtractor:
    def __init__(self):
        self.state = 'WAIT'
        self.buffer = ""
    def extract(self, token: str) -> str:
        self.buffer += token
        if self.state == 'WAIT':
            m = re.search(r'"speech"\s*:\s*"', self.buffer)
            if m: self.state = 'EXT'; self.buffer = self.buffer[m.end():]
            return ""
        if self.state == 'EXT':
            m = re.search(r'(?<!\\)"', self.buffer)
            if m: self.state = 'DONE'; res = self.buffer[:m.start()]; self.buffer = self.buffer[m.end():]; return res
            if len(self.buffer) > 1: res = self.buffer[:-1]; self.buffer = self.buffer[-1:]; return res
        return ""

async def handle_esp32_connection(websocket):
    print(f"🔌 [SERVEUR] Connexion: {websocket.remote_address}")
    # OPT-003: Informer l'ESP32 du début de calibration
    await websocket.send(json.dumps({'status': 'connected', 'message': 'Calibration en cours...'}))
    
    session = RobotSession(websocket)
    extractor = JSONSpeechExtractor()

    async def run_llm_and_tts(text: str):
        session.robot_is_answering = True
        session.interrupt_flag = False
        try: # FIX-009: Robustesse par try/finally
            session.history.append({"role": "user", "content": text})
            session.trim_history()
            await session.send_json("status", "thinking")
            
            # FIX-012: Pacing dynamique strict (Token Bucket ou timing réel)
            BYTES_PER_SAMPLE = 2
            # On envoie des petits morceaux (ex: 1024 octets = 512 samples = ~23ms d'audio)
            SMALL_CHUNK = 1024
            chunk_duration = (SMALL_CHUNK / BYTES_PER_SAMPLE) / SAMPLE_RATE_TTS

            # Pacing très légèrement accéléré (0.95) pour éviter que le buffer ESP32 ne se vide,
            # mais assez lent pour ne pas déborder sa file d'attente
            pacing = chunk_duration * 0.95

            async with llm_lock:
                proc = await asyncio.create_subprocess_exec(
                    PIPER_BIN, "--model", PIPER_MODEL, "--output_raw",
                    stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL
                )

                async def pipe_out():
                    try:
                        start_time = time.time()
                        total_sent = 0
                        while True:
                            data = await proc.stdout.read(SMALL_CHUNK)
                            if not data or session.interrupt_flag: break

                            await session.ws.send(data)
                            total_sent += len(data)

                            # Temps théorique où on devrait être
                            target_time = start_time + (total_sent / BYTES_PER_SAMPLE / SAMPLE_RATE_TTS) * 0.95
                            now = time.time()
                            if target_time > now:
                                await asyncio.sleep(target_time - now)
                    except: pass

                out_task = asyncio.create_task(pipe_out())
                await session.send_json("status", "speaking")
                
                gen = await asyncio.to_thread(llm.create_chat_completion, messages=session.history, stream=True)
                full_resp = ""
                sentence_buffer = ""
                for chunk in gen:
                    if session.interrupt_flag: break
                    token = chunk["choices"][0].get("delta", {}).get("content", "")
                    full_resp += token
                    sentence_buffer += token
                    
                    if any(p in token for p in ".!?"):
                        proc.stdin.write(sentence_buffer.replace('\\n',' ').encode())
                        await proc.stdin.drain()
                        sentence_buffer = ""

                # OPT-005: Extraire emotion, command et title dès la fin du flux
                try:
                    json_match = re.search(r'\{.*\}', full_resp, re.DOTALL)
                    if json_match:
                        parsed = json.loads(json_match.group())
                        if 'emotion' in parsed: await session.send_json('emotion', parsed['emotion'])
                        if 'command' in parsed and parsed['command'] != 'none': 
                            await session.send_json('command', parsed['command'])
                            if 'title' in parsed: await session.send_json('play_title', parsed['title'])
                except: pass

                if sentence_buffer.strip() and not session.interrupt_flag:
                    proc.stdin.write(sentence_buffer.encode()); await proc.stdin.drain()
                
                # FIX-010: Nettoyage Piper
                proc.stdin.close()
                out_task.cancel()
                try: await out_task
                except asyncio.CancelledError: pass
                await proc.wait()

            if full_resp and not session.interrupt_flag:
                session.history.append({"role": "assistant", "content": full_resp})
        except Exception as e:
            print(f"❌ [LLM/TTS] Erreur: {e}")
            await session.send_json("status", "error")
        finally:
            session.robot_is_answering = False
            await session.send_json("status", "idle")

    try:
        async for msg in websocket:
            if not isinstance(msg, bytes): continue
            chunk = np.frombuffer(msg, dtype=np.int16)
            rms = np.sqrt(np.mean(chunk.astype(np.float32)**2)) / 32768.0
            
            if not session.calibrated:
                session.calib_samples.append(rms)
                session.calib_count += 1
                if session.calib_count > 50:
                    session.ambient_noise_rms = float(np.percentile(session.calib_samples, 10))
                    session.calibrated = True
                    print(f"✅ Calibré (p10): {session.ambient_noise_rms:.4f}")
                continue

            v_ts = get_speech_timestamps(torch.from_numpy(chunk.astype(np.float32)/32768.0), vad_model, sampling_rate=SAMPLE_RATE_MIC, threshold=0.3)
            voice = len(v_ts) > 0
            
            if not session.is_speaking:
                session.pre_roll.append(chunk)
                b_threshold = max(0.03, session.ambient_noise_rms * 3)
                if voice and (not session.robot_is_answering or rms > b_threshold):
                    session.is_speaking = True
                    session.recording_start_time = time.time()
                    session.audio_buffer = list(session.pre_roll)
                    if session.robot_is_answering: session.interrupt_flag = True
            else:
                session.audio_buffer.append(chunk)
                if not voice: session.silence_frames += 1
                else: session.silence_frames = 0
                
                if session.silence_frames > 20 or (time.time() - session.recording_start_time > 30):
                    session.is_speaking = False
                    audio_data = np.concatenate(session.audio_buffer).astype(np.float32) / 32768.0
                    def run_whisper():
                        # Use translate task without forcing language. This allows Whisper to detect
                        # languages like Arabic/Darija, translate them to English, and pass to LLM.
                        segs, _ = whisper.transcribe(audio_data, task="translate", beam_size=5, vad_filter=True)
                        return "".join([s.text for s in segs]).strip()
                    text = await asyncio.to_thread(run_whisper)
                    if text: 
                        print(f"📝 [WHISPER] {text}")
                        asyncio.create_task(run_llm_and_tts(text))
                    session.audio_buffer = []; session.silence_frames = 0
    except websockets.exceptions.ConnectionClosed as e:
        print(f"⚠️ [SERVEUR] ESP32 déconnecté (Code {e.code})")
    except Exception as e:
        print(f"⚠️ [SERVEUR] Erreur inattendue: {e}")
    finally:
        print(f"🔌 [SERVEUR] Session terminée: {websocket.remote_address}")

async def main():
    # FIX-002: Désactiver ping_interval/timeout pour éviter les fermetures prématurées
    # Nous ajoutons aussi un buffer plus large pour mieux absorber les légers pics réseau
    async with websockets.serve(handle_esp32_connection, "0.0.0.0", 8765, ping_interval=None, ping_timeout=None, max_size=2**20, write_limit=2**20):
        print("\n🚀 [SERVEUR] Prêt ! Port 8765")
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
