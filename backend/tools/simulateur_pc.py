import asyncio
import websockets
import sounddevice as sd
import numpy as np
import json

SERVER_URI = "ws://127.0.0.1:8765"
MIC_SAMPLE_RATE = 16000
SPK_SAMPLE_RATE = 22050
CHUNK_SIZE = 1024

async def audio_sender(websocket):
    """Capture l'audio du micro PC et l'envoie en continu (comme l'INMP441)"""
    loop = asyncio.get_event_loop()
    q = asyncio.Queue()

    def callback(indata, frames, time, status):
        if status:
            print(status, flush=True)
        # Convertir en int16 (binaire brut) comme le fait l'ESP32
        pcm_data = (indata[:, 0] * 32767).astype(np.int16).tobytes()
        loop.call_soon_threadsafe(q.put_nowait, pcm_data)

    stream = sd.InputStream(samplerate=MIC_SAMPLE_RATE, channels=1, callback=callback)
    with stream:
        print("🎤 [Micro] Enregistrement en cours... Parlez !")
        while True:
            data = await q.get()
            await websocket.send(data)

async def message_receiver(websocket):
    """Reçoit l'audio de Piper et le JSON (comme le MAX98357A et l'écran TFT)"""
    stream = sd.OutputStream(samplerate=SPK_SAMPLE_RATE, channels=1, dtype='int16')
    with stream:
        async for message in websocket:
            if isinstance(message, bytes):
                # Le serveur envoie de l'audio PCM brut
                audio_data = np.frombuffer(message, dtype=np.int16)
                stream.write(audio_data)
            else:
                # Le serveur envoie un JSON de commande
                try:
                    data = json.loads(message)
                    if "emotion" in data:
                        print(f"\n🖥️ [Écran TFT Simulé] Émotion : {data['emotion']}")
                    if "speech" in data:
                        print(f"🤖 [Robot] Dit : {data['speech']}")
                    if "status" in data:
                        print(f"⚡ [Status] {data['status']}")
                except json.JSONDecodeError:
                    print(f"Message texte non-JSON: {message}")

async def main():
    try:
        async with websockets.connect(SERVER_URI) as websocket:
            print(f"✅ Connecté au serveur {SERVER_URI}")

            # Lancer l'envoi et la réception en parallèle
            sender_task = asyncio.create_task(audio_sender(websocket))
            receiver_task = asyncio.create_task(message_receiver(websocket))

            done, pending = await asyncio.wait(
                [sender_task, receiver_task],
                return_when=asyncio.FIRST_COMPLETED,
            )
            for task in pending:
                task.cancel()
    except Exception as e:
        print(f"❌ Erreur de connexion: {e}")

if __name__ == "__main__":
    try:
        print("Initialisation du simulateur ESP32 sur PC...")
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nArrêt du simulateur.")
