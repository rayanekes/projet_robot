import os
import sys
import json
import re
import asyncio
import queue
import numpy as np
import sounddevice as sd
import scipy.io.wavfile as wav

# Ajouter le chemin parent pour pouvoir importer la configuration si besoin
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(BASE_DIR)

import torch
from faster_whisper import WhisperModel
from llama_cpp import Llama
from silero_vad import load_silero_vad, get_speech_timestamps
from collections import deque

# =========================
# CONFIGURATION
# =========================
# Le dossier des modèles lourds est mutualisé à l'extérieur du dépôt Git pour éviter la duplication
MODELS_DIR = os.getenv("MODELS_DIR", os.path.join(BASE_DIR, "models"))
INPUT_WAV = os.path.join(BASE_DIR, "test_input.wav")

SAMPLE_RATE_MIC = 16000
SAMPLE_RATE_TTS = 22050

LLM_MODEL_PATH = os.path.join(MODELS_DIR, "qwen2.5-7b-instruct-q4_k_m.gguf")
LLM_MODEL_PATH_SPLIT = os.path.join(MODELS_DIR, "qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf")
WHISPER_MODEL = "medium"
WHISPER_DEVICE = "cuda"

import shutil

PIPER_BIN = "piper" # Utilise la commande installée via pip (piper-tts)
PIPER_MODEL = os.path.join(BASE_DIR, "piper", "fr_FR-siwis-medium.onnx")

SYSTEM_PROMPT = (
    "You are an interactive engineering robot assistant. "
    "The user's input will be provided in English (translated from Moroccan Darija and French). "
    "You must understand perfectly, BUT you MUST reply ONLY in pure and natural French. "
    "Never generate words in Arabic or English in your spoken response. "
    "You MUST ALWAYS respond with a strictly valid JSON object in the following format:\n"
    "{\n  \"speech\": \"Texte en français pur pour le robot.\",\n  \"emotion\": \"joie|neutre|triste\"\n}\n"
    "Return NOTHING except the valid JSON."
)

# =========================
# INITIALISATION ML PARALLÈLE
# =========================
import concurrent.futures

if os.path.exists(LLM_MODEL_PATH_SPLIT):
    actual_llm_path = LLM_MODEL_PATH_SPLIT
elif os.path.exists(LLM_MODEL_PATH):
    actual_llm_path = LLM_MODEL_PATH
else:
    print(f"\n❌ ERREUR CRITIQUE : Modèle IA introuvable dans '{MODELS_DIR}' !")
    print(f"Le fichier attendu est : '{os.path.basename(LLM_MODEL_PATH)}' OU '{os.path.basename(LLM_MODEL_PATH_SPLIT)}'")
    sys.exit(1)

if not shutil.which(PIPER_BIN):
    print("\n❌ ERREUR CRITIQUE : La commande système 'piper' est introuvable !")
    print("-> Exécutez : pip install piper-tts")
    sys.exit(1)

if not os.path.exists(PIPER_MODEL):
    print(f"\n❌ ERREUR CRITIQUE : Modèle de voix introuvable dans '{PIPER_MODEL}'.")
    print("-> Placez le fichier 'fr_FR-siwis-medium.onnx' dans le dossier 'backend/piper/'.")
    sys.exit(1)

if not os.path.exists(PIPER_MODEL + ".json"):
    print(f"\n❌ ERREUR CRITIQUE : Fichier de configuration Piper introuvable !")
    print(f"Le fichier attendu est : '{PIPER_MODEL}.json'.")
    print("-> Chaque voix Piper (.onnx) doit OBLIGATOIREMENT être accompagnée de son fichier de configuration (.json).")
    sys.exit(1)

print("⏳ Chargement des modèles IA en parallèle...")

def load_whisper():
    try:
        model = WhisperModel(WHISPER_MODEL, device=WHISPER_DEVICE, compute_type="int8_float16")
        print("✅ Whisper chargé (GPU - Optimisé VRAM)")
        return model
    except Exception as e:
        model = WhisperModel(WHISPER_MODEL, device="cpu", compute_type="int8")
        print("⚠️ Whisper chargé (CPU)")
        return model

def load_llama():
    try:
        import llama_cpp
        if not llama_cpp.llama_supports_gpu_offload():
            print("\n⚠️ AVERTISSEMENT : llama-cpp-python tourne sur le CPU !")

        model = Llama(
            model_path=actual_llm_path,
            n_gpu_layers=20,
            n_ctx=4096,
            verbose=False,
            # Charge les poids via la mémoire virtuelle du système (Memory Mapping) pour réduire la RAM système
            use_mmap=True,
            use_mlock=False
        )
        print("✅ LLaMA chargé (GPU)")
        return model
    except Exception as e:
        print(f"\n❌ Erreur LLaMA : {e}")
        sys.exit(1)

def load_vad():
    model = load_silero_vad()
    print("✅ Silero VAD chargé")
    return model

# Lancement parallèle via ThreadPoolExecutor
with concurrent.futures.ThreadPoolExecutor() as executor:
    future_w = executor.submit(load_whisper)
    future_l = executor.submit(load_llama)
    future_v = executor.submit(load_vad)

    whisper = future_w.result()
    llm = future_l.result()
    vad_model = future_v.result()

# =========================
# OUTILS D'EXTRACTION
# =========================
class JSONSpeechExtractor:
    def __init__(self):
        self.state = 'WAIT'
        self.buffer = ""

    def extract_chunk(self, token):
        self.buffer += token
        if self.state == 'WAIT':
            match = re.search(r'"speech"\s*:\s*"', self.buffer)
            if match:
                self.state = 'EXTRACTING'
                self.buffer = self.buffer[match.end():]
            elif len(self.buffer) > 100:
                self.buffer = self.buffer[-50:]
            return ""

        if self.state == 'EXTRACTING':
            match = re.search(r'(?<!\\)"', self.buffer)
            if match:
                self.state = 'FINISHED'
                speech = self.buffer[:match.start()]
                self.buffer = self.buffer[match.end():]
                return speech
            if len(self.buffer) > 1:
                speech = self.buffer[:-1]
                self.buffer = self.buffer[-1:]
                return speech
        return ""

def split_tts_sentence(buffer):
    match = re.search(r'([.?!]+)', buffer)
    if match and match.end() >= 10:
        return buffer[:match.end()], buffer[match.end():]
    return None, buffer

# =========================
# FONCTIONS DU PIPELINE
# =========================

def run_stt():
    print("🧠 [2/4] TRANSCRIPTION (Faster-Whisper)...")
    custom_vocab = "Terminale STE, ADC, ATC, PE, Transmettre, ESP32."
    prompt_darija_tech = f"Bonjour. Kidayr labas? Wach nbedaw l'installation dial le serveur? {custom_vocab}"

    segments, _ = whisper.transcribe(
        INPUT_WAV,
        task="translate",
            beam_size=5,
        initial_prompt=prompt_darija_tech
    )
    text = "".join([s.text for s in segments]).strip()
    print(f"   -> Traduction : '{text}'")
    return text

# Variables globales pour la gestion des interruptions
robot_is_answering = False
interrupt_flag = False
conversation_history = [{"role": "system", "content": SYSTEM_PROMPT}]

async def read_piper_stdout(piper_proc, state_container):
    try:
        stream = sd.OutputStream(samplerate=SAMPLE_RATE_TTS, channels=1, dtype='int16')
        with stream:
            while True:
                if state_container["interrupt"]:
                    piper_proc.terminate()
                    break
                audio_out = await piper_proc.stdout.read(4096)
                if not audio_out:
                    break
                audio_data = np.frombuffer(audio_out, dtype=np.int16)
                stream.write(audio_data)
                await asyncio.sleep(0.01)
    except Exception:
        pass

async def run_llm_and_tts(user_text):
    global robot_is_answering, interrupt_flag
    robot_is_answering = True
    interrupt_flag = False

    print("\n🤖 [3/4] GÉNÉRATION (LLaMA) & SYNTHÈSE (Piper)...")
    print("   [ÉCRAN] -> Affichage de reflexion_1.bmp")

    conversation_history.append({"role": "user", "content": user_text})
    extractor = JSONSpeechExtractor()
    tts_buffer = ""
    full_llm_response = ""
    emotion_sent = False

    piper_proc = await asyncio.create_subprocess_exec(
        PIPER_BIN, "--model", PIPER_MODEL, "--output_raw",
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL
    )

    state_container = {"interrupt": False}
    stream_task = asyncio.create_task(read_piper_stdout(piper_proc, state_container))

    def generate_llm_stream():
        return llm.create_chat_completion(
            messages=conversation_history,
            max_tokens=200,
            temperature=0.7,
            stream=True
        )

    print("   [ÉCRAN] -> Affichage de parle_1.bmp")
    stream_generator = await asyncio.to_thread(generate_llm_stream)

    while True:
        if interrupt_flag:
            print("   ⚠️ INTERRUPTION (Barge-in) !")
            state_container["interrupt"] = True
            break

        try:
            chunk = await asyncio.to_thread(next, stream_generator)
        except StopIteration:
            break

        delta = chunk["choices"][0].get("delta", {})
        if "content" in delta:
            token = delta["content"]
            full_llm_response += token

            # Simulation d'envoi d'émotion à l'écran
            if not emotion_sent:
                emotion_match = re.search(r'"emotion"\s*:\s*"([^"]+)"', full_llm_response)
                if emotion_match:
                    emotion = emotion_match.group(1)
                    print(f"   [ÉCRAN] -> Changement d'émotion : {emotion}_1.bmp")
                    emotion_sent = True

            speech_part = extractor.extract_chunk(token)
            if speech_part:
                speech_part = speech_part.replace('\\n', ' ').replace('\\"', '"')
                tts_buffer += speech_part

                sentence, tts_buffer = split_tts_sentence(tts_buffer)
                if sentence:
                    piper_proc.stdin.write(sentence.encode("utf-8"))
                    await piper_proc.stdin.drain()

    if tts_buffer.strip() and not interrupt_flag:
        piper_proc.stdin.write(tts_buffer.encode("utf-8"))
        await piper_proc.stdin.drain()
        piper_proc.stdin.close()

    if interrupt_flag:
        if not piper_proc.stdin.is_closing():
            piper_proc.stdin.close()

    await stream_task

    if piper_proc.returncode is None:
        piper_proc.terminate()

    robot_is_answering = False
    print("   [ÉCRAN] -> Retour au repos (idle_1.bmp)")
    print("✅ Cycle terminé.\n" + "="*50)

    if full_llm_response and not interrupt_flag:
        conversation_history.append({"role": "assistant", "content": full_llm_response})

async def main():
    global robot_is_answering, interrupt_flag
    print("\n🎧 En écoute continue... Parlez dans le micro du PC.")

    q = queue.Queue()
    def callback(indata, frames, time, status):
        if status: pass
        q.put(indata.copy())

    stream = sd.InputStream(samplerate=SAMPLE_RATE_MIC, channels=1, dtype='int16', callback=callback)

    is_speaking = False
    audio_buffer = []
    pre_roll = deque(maxlen=8)
    silence_frames = 0
    silence_threshold = 20

    with stream:
        while True:
            # Récupère le chunk audio sans bloquer l'Event Loop
            chunk = await asyncio.to_thread(q.get)

            # On flatten() pour transformer le [N, 1] de sounddevice en [N] (1D) pour Silero VAD
            audio_tensor = torch.from_numpy(chunk.flatten().astype(np.float32) / 32768.0)

            # Vad detection
            timestamps = await asyncio.to_thread(get_speech_timestamps, audio_tensor, vad_model, sampling_rate=SAMPLE_RATE_MIC, threshold=0.3)
            voice_detected = len(timestamps) > 0

            # AEC heuristique
            rms = np.sqrt(np.mean(audio_tensor.numpy()**2))

            if not is_speaking:
                pre_roll.append(chunk)
                if voice_detected:
                    barge_in_threshold = 0.05
                    if robot_is_answering and rms < barge_in_threshold:
                        continue # Écho potentiel

                    is_speaking = True
                    audio_buffer.extend(list(pre_roll))
                    pre_roll.clear()
                    silence_frames = 0
                    print("\n🎤 Voix détectée...")

                    if robot_is_answering:
                        interrupt_flag = True
            else:
                audio_buffer.append(chunk)
                if voice_detected:
                    silence_frames = 0
                else:
                    silence_frames += 1
                    if silence_frames > silence_threshold:
                        is_speaking = False
                        print("⏸️ Silence détecté, traitement...")

                        audio_data = np.concatenate(audio_buffer)
                        wav.write(INPUT_WAV, SAMPLE_RATE_MIC, audio_data)

                        text = await asyncio.to_thread(run_stt)
                        if text:
                            asyncio.create_task(run_llm_and_tts(text))

                        audio_buffer = []
                        silence_frames = 0

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nArrêt du testeur de pipeline.")
