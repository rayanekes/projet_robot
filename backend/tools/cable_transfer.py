import serial
import time
import os
import glob

PORT = '/dev/ttyUSB0'
BAUD = 115200

def send_file(ser, filepath, target_name):
    size = os.path.getsize(filepath)
    print(f"Envoi de {target_name} ({size} bytes)...")
    
    ser.write(f"START|{target_name}|{size}\n".encode())
    
    while True:
        resp = ser.readline().decode().strip()
        if "SEND_DATA" in resp: break
        if "OPEN_FAIL" in resp: 
            print("Erreur: Impossible d'ouvrir le fichier sur la SD")
            return False

    with open(filepath, 'rb') as f:
        while True:
            chunk = f.read(512)
            if not chunk: break
            ser.write(chunk)
            # Attendre l'ACK de l'ESP32 pour confirmer l'écriture du bloc
            while True:
                resp = ser.readline().decode().strip()
                if "ACK" in resp: break

    while True:
        resp = ser.readline().decode().strip()
        if "TRANSFER_DONE" in resp: 
            print("Succès !")
            return True

try:
    print("Connexion au port série (patience 5s)...")
    ser = serial.Serial(PORT, BAUD, timeout=1)
    
    # Force reboot
    ser.setDTR(False)
    time.sleep(0.5)
    ser.setDTR(True)
    
    t_end = time.time() + 5
    found = False
    while time.time() < t_end:
        ser.write(b"PING\n")
        resp = ser.readline().decode().strip()
        if "PONG" in resp or "SD_READY" in resp or "SD_MOUNT_FAIL" in resp:
            found = True
            break
        time.sleep(0.5)

    if not found:
        print("ESP32 ne répond pas au PING. Vérifiez le flashage.")
        exit(1)

    # 1. Envoyer les visages BMP
    bmps = glob.glob("/home/rayane/projet_robot/Local-AI-assistance/backend/tools/*.bmp")
    for b in bmps:
        send_file(ser, b, "/" + os.path.basename(b))

    # 2. Envoyer la musique (Skit)
    mp3 = "/home/rayane/Téléchargements/_lmorphine.  Skit _INK _2019(MP3_160K).mp3"
    if os.path.exists(mp3):
        send_file(ser, mp3, "/test.mp3")
    else:
        print("MP3 non trouvé.")

    print("\n✅ TOUT EST SUR LA CARTE SD !")
    ser.close()

except Exception as e:
    print(f"Erreur: {e}")
