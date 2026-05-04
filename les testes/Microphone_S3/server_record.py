import asyncio
import websockets
import wave
import sys

AUDIO_FILE = "enregistrement.wav"
SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2 # 16-bit

audio_buffer = bytearray()

async def audio_receiver(websocket):
    global audio_buffer
    print("\n🔌 ESP32-S3 Connecté au serveur de réception ! (Microphone)")
    print("⏳ En attente de la réception des données...")
    
    audio_buffer.clear()
    
    try:
        async for message in websocket:
            if isinstance(message, str):
                if message.startswith("FILENAME:"):
                    AUDIO_FILE = message.split(":")[1].replace(".raw", ".wav")
                    print(f"\n📥 Préparation à la réception de : {AUDIO_FILE}")
                    audio_buffer.clear()
            
            elif isinstance(message, bytes):
                if message == b"EOF":
                    print("\n\n✅ Enregistrement hors-ligne complet reçu !")
                    save_wav(AUDIO_FILE)
                    print(f"Volume compressé à {len(audio_buffer) / 1024:.1f} Ko")
                    print("Vous pouvez écouter le fichier :", AUDIO_FILE)
                else:
                    audio_buffer.extend(message)
                    print(f"\r📡 Reçu : {len(audio_buffer) / 1024:.1f} Ko", end="", flush=True)
    except websockets.exceptions.ConnectionClosed:
        print("\n❌ ESP32 Déconnecté.")
        if len(audio_buffer) > 0:
            save_wav(AUDIO_FILE)

def save_wav(filename):
    print(f"\n💾 Sauvegarde dans {filename}...")
    with wave.open(filename, 'wb') as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio_buffer)
    print("✅ Fichier sauvegardé avec succès.")

async def main():
    print("🚀 Serveur d'Enregistrement Démarré sur ws://0.0.0.0:8766")
    async with websockets.serve(audio_receiver, "0.0.0.0", 8766):
        await asyncio.Future()  # Tourne à l'infini

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nFermeture...")
