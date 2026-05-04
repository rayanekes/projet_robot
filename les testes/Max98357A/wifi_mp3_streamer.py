import asyncio
import websockets
import subprocess
import sys

# Configuration
SAMPLE_RATE = 24000
CHUNK_SIZE = 4096  # bytes (2048 samples en 16-bit mono = ~85ms)

PLAYLIST = [
    "/home/rayane/Téléchargements/STORMY feat. Inkonnu - MIZANE (Prod. Mobench x Neyl)(MP3_160K).mp3",
    "/home/rayane/Téléchargements/OrelSan - Du propre [CLIP OFFICIEL](MP3_160K).mp3",
    "/home/rayane/Téléchargements/Shonen(MP3_160K).mp3",
    "/home/rayane/Téléchargements/Orelsan - Encore une fois ft. _yamebantu (lyrics video)(MP3_160K).mp3"
]

current_process = None
playing_idx = -1
is_playing = False

def get_ffmpeg_cmd(filepath):
    # Décode l'audio en PCM 16-bit, Mono, 24000 Hz vers la sortie standard
    return [
        'ffmpeg', '-hide_banner', '-loglevel', 'error',
        '-i', filepath,
        '-f', 's16le', '-acodec', 'pcm_s16le', '-ar', str(SAMPLE_RATE), '-ac', '1',
        'pipe:1'
    ]

async def audio_streamer(websocket):
    global current_process, is_playing
    print("\n🔌 ESP32 Connecté au serveur !")
    
    try:
        while True:
            if is_playing and current_process and current_process.stdout:
                # Lire un chunk décodé par ffmpeg de manière asynchrone
                loop = asyncio.get_running_loop()
                chunk = await loop.run_in_executor(None, current_process.stdout.read, CHUNK_SIZE)
                
                if not chunk:
                    # Fin du fichier
                    print("\n🎵 Fin du titre. Passage au suivant...")
                    next_track()
                    continue
                
                # Baisse du volume (division de l'amplitude par 4) pour éviter de faire crasher l'ESP32
                # Convertir les bytes en tableau d'entiers 16 bits, diviser, puis remettre en bytes
                import array
                samples = array.array('h', chunk)
                for i in range(len(samples)):
                    samples[i] = int(samples[i] / 4)
                
                # Envoyer le chunk via Wi-Fi
                await websocket.send(samples.tobytes())
                print(".", end="", flush=True)
                
                # Pacing : on limite la vitesse d'envoi pour être proche du temps réel
                # Cela évite de saturer le buffer réseau et permet de changer de musique instantanément
                await asyncio.sleep(0.080)
            else:
                await asyncio.sleep(0.1)

    except websockets.exceptions.ConnectionClosed:
        print("\n❌ ESP32 Déconnecté")

def play_track(idx):
    global current_process, playing_idx, is_playing
    
    if current_process:
        current_process.kill()
        current_process = None
        
    if 0 <= idx < len(PLAYLIST):
        playing_idx = idx
        filepath = PLAYLIST[idx]
        print(f"\n▶️ Lecture en cours : {filepath.split('/')[-1]}")
        cmd = get_ffmpeg_cmd(filepath)
        current_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        is_playing = True
    else:
        print("\n❌ Numéro de musique invalide.")

def stop_track():
    global current_process, is_playing
    if current_process:
        current_process.kill()
        current_process = None
    is_playing = False
    print("\n⏹️ Lecture arrêtée.")

def next_track():
    global playing_idx
    if playing_idx != -1:
        play_track((playing_idx + 1) % len(PLAYLIST))

def print_menu():
    print("\n--- 🎛️ CONTRÔLEUR AUDIO WI-FI ---")
    for i, track in enumerate(PLAYLIST):
        print(f"[{i}] {track.split('/')[-1]}")
    print("\nCommandes : [Numéro] = Jouer | [s] = Stopper | [n] = Suivant | [q] = Quitter")
    print("Votre choix : ", end="", flush=True)

async def user_input_loop():
    loop = asyncio.get_running_loop()
    while True:
        print_menu()
        choice = await loop.run_in_executor(None, sys.stdin.readline)
        choice = choice.strip().lower()
        
        if choice == 'q':
            stop_track()
            print("👋 Bye!")
            sys.exit(0)
        elif choice == 's':
            stop_track()
        elif choice == 'n':
            next_track()
        elif choice.isdigit():
            play_track(int(choice))

async def main():
    print(f"🚀 Serveur Wi-Fi Démarré sur ws://0.0.0.0:8765")
    
    # Démarrer le serveur websocket en tâche de fond
    server = await websockets.serve(audio_streamer, "0.0.0.0", 8765)
    
    # Démarrer la boucle d'input utilisateur
    await user_input_loop()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nFermeture...")
