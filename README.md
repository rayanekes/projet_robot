# 🤖 Projet Robot Assistant (ESP32-S3 + Serveur AI)

[![GitHub License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Python](https://img.shields.io/badge/Python-3.10+-green.svg)](https://www.python.org/)

Un assistant robotique intelligent et conversationnel basé sur une architecture distribuée : un cerveau IA sur serveur Python et un corps réactif sur ESP32-S3.

---

## 🌟 Fonctionnalités Clés

- **🗣️ Conversation Naturelle** : Pipeline complet STT (Whisper) -> LLM (Qwen/Gemma) -> TTS (Piper/Kokoro).
- **🚀 Ultra-Basse Latence** : Streaming audio full-duplex via WebSockets.
- **🎭 Expressions Faciales** : Rendu fluide d'émotions sur écran TFT ST7789.
- **🎮 Contrôle Infrarouge** : Pilotage des modes et personnalités via télécommande IR.
- **🧠 Mémoire à Long Terme** : Système RAG pour une persistance des conversations.
- **📡 Monitoring Distant** : Système de log distant pour déboguer l'ESP32 sans câble.

---

## 📂 Structure du Projet

```text
├── backend/                  # Cerveau du robot (Python)
│   ├── src/                  # Logique serveur, LLM et TTS
│   ├── voices/               # Profils vocaux et clones
│   ├── logs/                 # Traces d'exécution et logs distants
│   └── requirements.txt      # Dépendances Python
│
├── firmware/                 # Corps du robot (ESP32-S3 N16R8)
│   ├── src/                  # Driver audio, affichage et WS
│   ├── include/              # Configuration matérielle
│   └── platformio.ini        # Config PlatformIO
│
├── tools/                    # Utilitaires (Mapping IR, tests)
├── les testes/               # Bancs d'essai unitaires matériel
└── scripts/                  # Scripts de démarrage (start_server.sh, etc.)
```

---

## 💻 Installation du Backend

### 1. Prérequis Système
Installez les dépendances audio pour Linux (Ubuntu/Pop!_OS) :
```bash
sudo apt-get update && sudo apt-get install -y portaudio19-dev libavformat-dev libavcodec-dev
```

### 2. Environnement Python
```bash
cd backend
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### 3. 📥 Modèles IA (Obligatoire)
Les modèles ne sont pas inclus sur GitHub (>10Mo). Téléchargez-les et placez-les comme suit :
- **LLM** : `backend/models/qwen2.5-7b-instruct-q4_k_m.gguf`
- **TTS** : `backend/models/piper/fr_FR-siwis-medium.onnx`
- **Vocal** : `backend/models/kokoro-v1.0.onnx`

---

## 🔌 Matériel & Câblage (ESP32-S3)

Le projet est optimisé pour un **ESP32-S3 N16R8** (16MB Flash, 8MB PSRAM).

| Composant | Signal | GPIO | Note |
| :--- | :--- | :--- | :--- |
| **Micro (INMP441)** | SCK / WS / SD | 32 / 25 / 33 | I2S Input |
| **HP (MAX98357A)** | BCLK / LRC / DIN | 27 / 26 / 12 | I2S Output |
| **Écran (ST7789)** | SCL / SDA / CS | 18 / 23 / 14 | SPI Bus |
| **Infrarouge** | IR Recv | 16 | Contrôle à distance |

> [!IMPORTANT]
> **Alimentation** : Utilisez une source 5V/2A stable. L'activation simultanée du Wi-Fi et de l'audio I2S provoque des brownouts sur les alimentations faibles.

---

## 🚀 Démarrage Rapide

1. **Lancer le serveur** :
   ```bash
   ./start_server.sh
   ```
2. **Flasher l'ESP32** : Utilisez VSCode + PlatformIO pour uploader le dossier `firmware`.

---

## 🛠️ Outils Utilitaires

- `tools/ir_mapper.py` : Pour mapper les touches de votre télécommande IR.
- `convert_voice.sh` : Script pour préparer des échantillons vocaux pour le clonage.
- `test/test_pipeline.py` : Teste la chaîne IA complète sur votre PC.

---

## 📄 Licence
Ce projet est sous licence MIT. Libre à vous de l'utiliser et de le modifier !
glé à 5.0V.

### Prérequis Logiciels
- VSCode avec l'extension PlatformIO
- Une carte ESP32

Ouvrez le dossier `firmware` dans VSCode, puis compilez et téléversez le code.
