import subprocess
import serial
import time
import os
import glob

# Téléversement du code
print("Étape 1: Téléversement du Serveur Wi-Fi dans l'ESP32...")
subprocess.run(["/home/rayane/tts_env/bin/pio", "run", "-t", "upload"], cwd="/home/rayane/projet_robot/Local-AI-assistance/firmware", check=True)

# Lecture de l'IP
print("Étape 2: Attente de la connexion Wi-Fi...")
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
ser.dtr = False
ser.rts = False
time.sleep(1)
ser.dtr = True
ser.rts = True

ip = None
t_end = time.time() + 20
while time.time() < t_end:
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    if line:
        print("ESP32:", line)
        if line.startswith("WIFI_IP:"):
            ip = line.split(":")[1]
            break
        if "SD Mount Failed" in line:
            print("ERREUR : La carte SD n'a pas pu s'initialiser. Vérifiez qu'elle est bien insérée et redémarrez.")
            exit(1)

ser.close()

if not ip:
    print("Erreur : Impossible de récupérer l'adresse IP. Le Wi-Fi est-il correct ?")
    exit(1)

print(f"\nÉtape 3: ESP32 connecté ! Envoi des fichiers sur l'IP : {ip} (Cela prendra quelques secondes)")

# Upload BMPs
bmp_files = glob.glob("/home/rayane/projet_robot/Local-AI-assistance/backend/tools/*.bmp")
for f in bmp_files:
    print(f"-> Transfert de {os.path.basename(f)}...")
    subprocess.run(["curl", "-s", "-F", f"f=@{f}", f"http://{ip}/upload"])

# Upload MP3
mp3_file = "/home/rayane/Téléchargements/_lmorphine.  Skit _INK _2019(MP3_160K).mp3"
if os.path.exists(mp3_file):
    print(f"-> Transfert de la musique (sera renommée en /music/test.mp3 sur la SD)...")
    subprocess.run(["curl", "-s", "-F", f"f=@{mp3_file}", f"http://{ip}/upload"])
else:
    print(f"Le fichier MP3 n'a pas été trouvé dans vos Téléchargements.")

print("\n🎉 TOUS LES TRANSFERTS SONT TERMINÉS AVEC SUCCÈS !")