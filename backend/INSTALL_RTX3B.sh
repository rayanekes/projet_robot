#!/bin/bash

# Configuration du robot pour GPU RTX 3B

echo "🤖 Installation Robot IA - RTX 3B Edition"
echo "=========================================="

# 1. Vérifier CUDA
echo "✓ Vérification CUDA..."
nvidia-smi

# 2. Installer les dépendances système
echo "✓ Installation des dépendances système..."
sudo apt-get update
sudo apt-get install -y portaudio19-dev libavformat-dev libavcodec-dev

# 3. Backend Python
echo "✓ Installation des dépendances Python..."
cd backend
pip install -r requirements.txt

# 4. Modèles (À télécharger manuellement)
echo "⚠️  Modèles requis (téléchargement manuel):"
echo "   - backend/models/qwen2.5-7b-instruct-q4_k_m.gguf (3.5 GB)"
echo "   - backend/piper/fr_FR-siwis-medium.onnx (100 MB)"
echo "   - backend/piper/fr_FR-siwis-medium.onnx.json"

# 5. Configuration XTTS (optionnel)
echo ""
echo "🎵 Configuration TTS:"
echo "   Piper: export TTS_ENGINE=piper"
echo "   XTTS v2: export TTS_ENGINE=xtts"
echo ""
echo "Par défaut: Piper"

# 6. Démarrage serveur
echo ""
echo "🚀 Démarrage du serveur:"
echo "   python src/server.py"
echo ""
echo "Logs attendus:"
echo "   ✅ Silero VAD chargé"
echo "   ✅ Whisper chargé (GPU)"
echo "   ✅ XTTS v2 chargé (ou Piper)"
echo "   ✅ LLaMA chargé"
echo "   🚀 [SERVEUR] Prêt ! Port 8765"
