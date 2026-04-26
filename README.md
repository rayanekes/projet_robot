# Projet Robot Assistant (ESP32 + Serveur AI)

Ce projet divise l'architecture du robot en deux parties principales :
1. **Un Backend (Python) :** Serveur AI basé sur WebSocket avec traitement vocal (Whisper, Silero VAD), génération de texte (LLaMA) et synthèse vocale (Piper).
2. **Un Firmware (C++) :** Code embarqué pour l'ESP32 contrôlant le microphone, le haut-parleur (I2S), un écran TFT (ST7789) et des GPIOs.

## Structure du projet

```
├── backend/                  # Serveur d'intelligence artificielle
│   ├── src/                  # Code source (ex: server.py)
│   ├── models/               # Modèles LLM (à télécharger)
│   ├── piper/                # Exécutable et modèles Piper TTS (à télécharger)
│   ├── memory/               # Sauvegarde du contexte (RAG)
│   └── requirements.txt      # Dépendances Python
│
├── firmware/                 # Code embarqué pour ESP32
│   ├── src/                  # Fichiers sources C++
│   ├── include/              # Fichiers d'en-tête (headers)
│   └── platformio.ini        # Configuration PlatformIO
│
├── .gitignore                # Fichiers à ignorer par git
└── README.md                 # Cette documentation
```

## Backend
Le serveur utilise un réseau WebSocket pour communiquer avec l'ESP32.

### Prérequis (Serveur Pop!_OS / Ubuntu)
Pour que les outils de test audio (comme le simulateur PC ou la capture micro) fonctionnent sous Linux, vous devez d'abord installer les bibliothèques système de gestion du son :
```bash
sudo apt-get update
sudo apt-get install portaudio19-dev
sudo apt-get install libavformat-dev libavcodec-dev libavdevice-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev
```

Ensuite, installez toutes les dépendances Python :
```bash
cd backend
pip install -r requirements.txt
```

### ⚠️ IMPORTANT : Modèles IA (Non inclus sur GitHub)
Pour des raisons de taille de fichiers, les modèles d'Intelligence Artificielle (GGUF, ONNX) ne sont pas stockés sur GitHub. Vous devez les copier ou les télécharger manuellement dans les dossiers suivants :

1. **Modèle LLM (Llama.cpp) :**
   - Placez `qwen2.5-7b-instruct-q4_k_m.gguf` dans le dossier `backend/models/`.
2. **Modèle TTS (Piper ONNX) :**
   - Placez le fichier de voix `fr_FR-siwis-medium.onnx` (et son fichier `.json` associé) dans le dossier `backend/piper/`. L'exécutable lui-même est installé via pip (`piper-tts`).

### Exécution du Serveur Principal
```bash
python src/server.py
```

### Exécution des Tests Locaux (Micro PC)
Pour tester l'IA sans ESP32 :
```bash
python tools/simulateur_pc.py
# ou pour le pipeline pas à pas :
python test/test_pipeline.py
```

## Firmware (Câblage & Matériel)
Le code est conçu pour être compilé et flashé avec [PlatformIO](https://platformio.org/).

### Schéma de Câblage (Pinout ESP32)
Le robot requiert une connexion précise entre l'ESP32 et les périphériques I2S/SPI. Voici le tableau de connexion officiel :

| Composant | Broche Composant | Broche ESP32 (GPIO) | Notes |
| :--- | :--- | :--- | :--- |
| **Microphone (INMP441)** | L/R | GND | Pour sélectionner le canal gauche par défaut |
| | WS | 25 | Word Select (Horloge de canal) |
| | SCK | 32 | Serial Clock (Horloge binaire) |
| | SD | 33 | Serial Data (Données audio sortantes) |
| **Haut-Parleur (MAX98357A)**| LRC (WS) | 26 | Word Select |
| | BCLK | 27 | Bit Clock |
| | DIN | 22 | Data IN (Données audio entrantes) |
| | VIN | 5V / VIN | Attention: Préférable d'alimenter avec un Step-Down 5V externe |
| **Écran (ST7789 SPI)** | MOSI | 23 | Master Out Slave In |
| | MISO | 19 | Master In Slave Out |
| | SCK | 18 | SPI Clock |
| | CS | 14 | Chip Select (Écran) |
| | DC | 21 | Data/Command |
| | RST | 4 | Reset |
| **Lecteur Carte SD** | CS | 5 | Chip Select (Carte SD) |
| | MOSI, MISO, SCK | 23, 19, 18 | *Partagés avec le bus SPI de l'écran TFT* |

> **⚠️ AVERTISSEMENT ALIMENTATION :**
> Une seule batterie 18650 "générique" (3.7V) n'est **PAS** suffisante pour alimenter simultanément l'ESP32 (Wi-Fi), l'amplificateur audio et l'écran TFT. Les pics de courant (notamment lors de l'envoi Wi-Fi ou de l'audio) vont causer des chutes de tension (*Brownouts*) et redémarrer la carte.
> **Solution recommandée :** Utilisez un PowerBank 5V/2A branché en USB, ou 2 batteries 18650 en série (7.4V) régulées par un module Step-Down LM2596 réglé à 5.0V.

### Prérequis Logiciels
- VSCode avec l'extension PlatformIO
- Une carte ESP32

Ouvrez le dossier `firmware` dans VSCode, puis compilez et téléversez le code.
