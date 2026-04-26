import asyncio
import websockets
import sounddevice as sd
import numpy as np

# IP du serveur IA principal
IA_SERVER_URI = "ws://127.0.0.1:8765"

# Port sur lequel le Proxy va écouter l'ESP32
PROXY_PORT = 8766

MIC_SAMPLE_RATE = 16000

async def mic_to_server(ia_websocket):
    """Capture l'audio du PC et l'envoie au serveur IA à la place de l'ESP32."""
    loop = asyncio.get_event_loop()
    q = asyncio.Queue()

    def callback(indata, frames, time, status):
        if status:
            pass
        pcm_data = (indata[:, 0] * 32767).astype(np.int16).tobytes()
        loop.call_soon_threadsafe(q.put_nowait, pcm_data)

    stream = sd.InputStream(samplerate=MIC_SAMPLE_RATE, channels=1, callback=callback)
    with stream:
        print("🎤 [Micro PC Proxy] Enregistrement... Parlez !")
        while True:
            data = await q.get()
            await ia_websocket.send(data)

async def handle_esp32(esp_websocket):
    """Gère la connexion de l'ESP32.
    L'ESP32 se connecte à ce proxy.
    Le proxy se connecte au vrai serveur IA.
    Il redirige la réponse TTS de l'IA vers l'ESP32.
    """
    print("🔌 [Proxy] ESP32 Connecté au proxy !")
    try:
        async with websockets.connect(IA_SERVER_URI) as ia_websocket:
            print("🔗 [Proxy] Connecté au Serveur IA principal.")

            # Lancer la capture du micro du PC vers l'IA
            mic_task = asyncio.create_task(mic_to_server(ia_websocket))

            # Boucle pour lire les réponses de l'IA et les envoyer à l'ESP32
            async def ia_to_esp32():
                async for message in ia_websocket:
                    # On redirige tout (Audio I2S pour le MAX98357A + JSON pour le TFT) vers l'ESP32
                    await esp_websocket.send(message)

            esp_task = asyncio.create_task(ia_to_esp32())

            # On ignore l'audio provenant de l'ESP32 (puisque l'INMP441 n'est pas branché)
            async def ignore_esp32_audio():
                async for _ in esp_websocket:
                    pass

            ignore_task = asyncio.create_task(ignore_esp32_audio())

            done, pending = await asyncio.wait(
                [mic_task, esp_task, ignore_task],
                return_when=asyncio.FIRST_COMPLETED,
            )
            for task in pending:
                task.cancel()

    except Exception as e:
        print(f"❌ [Proxy] Erreur: {e}")
    finally:
        print("🔌 [Proxy] ESP32 Déconnecté.")

async def main():
    import socket
    print(f"🚀 Démarrage du Proxy Micro PC sur le port {PROXY_PORT}...")
    async with websockets.serve(handle_esp32, "0.0.0.0", PROXY_PORT, family=socket.AF_INET, reuse_address=True, reuse_port=True):
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nArrêt du Proxy.")
