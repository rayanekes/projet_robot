import asyncio
import websockets
import math
import struct
import time

# Configuration
SAMPLE_RATE = 24000
FREQ = 440.0 # 440 Hz (Note La)
AMPLITUDE = 8000 # Volume (max 32767)
CHUNK_SIZE = 4096 # 2048 samples (16-bit) par envoi

async def audio_streamer(websocket):
    print("🔌 ESP32 Connecté !")
    phase = 0.0
    phase_increment = 2.0 * math.pi * FREQ / SAMPLE_RATE

    try:
        while True:
            # Générer un chunk d'onde sinusoïdale pure (Mono, 16-bit)
            buffer = bytearray()
            for _ in range(CHUNK_SIZE // 2):
                sample = int(math.sin(phase) * AMPLITUDE)
                # Pack en 16-bit little-endian
                buffer.extend(struct.pack('<h', sample))
                phase += phase_increment
                if phase >= 2.0 * math.pi:
                    phase -= 2.0 * math.pi

            # Envoyer le chunk
            await websocket.send(buffer)
            
            # Pacing : Attendre le temps exact que dure ce chunk
            # 2048 samples à 24000 Hz = ~85.3 ms
            await asyncio.sleep(0.080) # Légèrement plus rapide pour remplir le buffer

    except websockets.exceptions.ConnectionClosed:
        print("❌ ESP32 Déconnecté")

async def main():
    print(f"🚀 Serveur de test Audio Wi-Fi démarré sur ws://0.0.0.0:8765")
    print(f"🎵 Génération d'une onde pure à {FREQ}Hz ({SAMPLE_RATE}Hz 16-bit)")
    async with websockets.serve(audio_streamer, "0.0.0.0", 8765):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    asyncio.run(main())
