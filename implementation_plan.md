# Plan d'Architecture Global : Migration ESP32-S3 (N16R8)

Ce document centralise toutes les idées et étapes d'implémentation prévues pour la nouvelle carte ESP32-S3 N16R8, afin d'être prêt pour lundi.

## 1. Matériel & Câblage (Hardware)
- **Carte Principale :** ESP32-S3-WROOM-1-N16R8 (16Mo Flash, 8Mo PSRAM Octal SPI).
- **Audio Out :** Module MAX98357A (Ampli I2S) branché sur le Haut-Parleur 6W 8Ω.
  > *Note technique : Le MAX98357A fournira environ 1.5W à 2W sous 8 ohms, ce qui sera déjà extrêmement fort et net pour un robot de bureau, sans risquer de griller le haut-parleur.*
- **Audio In :** Micro I2S (INMP441).
- **Contrôle :** Récepteur Infrarouge IR1838B.
- **Écran :** ST7789 TFT SPI (240×320).
- **Supprimés :** Tactile XPT2046, Carte SD — montage simplifié.
- **Optionnel :** HC-SR04 (capteur ultrasonique) disponible pour détection de présence.
- **Câblage :** Voir [CABLAGE_S3.md](./CABLAGE_S3.md) pour le mapping GPIO Matrix optimisé (2 breadboards, écran dégagé).

## 2. Nouvelles Fonctionnalités Logicielles (Firmware)

### A. Wake Word Local & Barge-In (Interruption vocale)
- **Remplacement du bouton tactile :** L'ESP32-S3 intègre les instructions vectorielles AI. Nous allons implémenter la librairie **ESP-SR (Espressif Speech Recognition)**.
- **Wake Word :** Le robot écoutera localement (sans WiFi) un mot clé (ex: "Hey Robot" ou "Alexa").
- **AEC (Acoustic Echo Cancellation) :** L'ESP32-S3 annulera le son de son propre haut-parleur dans le flux du micro. Cela permet au robot de t'entendre dire "Arrête !" *pendant* qu'il parle, permettant l'interruption vocale naturelle.

### B. Gestion de l'Énergie (Sleep Mode) & Télécommande IR
- **Mode Veille (Light Sleep) :** 
  - L'écran s'éteint (backlight LED → LOW via GPIO 14).
  - La connexion WebSocket se met en pause.
  - Seul le cœur dédié au micro (Wake Word) et le récepteur IR restent actifs pour économiser l'énergie et éviter la chauffe.
- **Contrôle du Volume :**
  - Mapping des touches Haut/Bas de ta télécommande IR pour ajuster le volume matériellement.
  - Le volume sera modifié en appliquant un gain multiplicateur directement sur les données PCM avant de les envoyer au MAX98357A en I2S.
- **Bouton Sleep :** Mapping d'un bouton de la télécommande pour forcer la mise en veille ou le réveil manuellement.

### C. Refonte des Buffers Mémoire (PSRAM) & Visage Moderne (16Mo Flash)
- **Audio I2S :** Grâce aux 8 Mo de PSRAM, nous allons allouer des **Ring Buffers** massifs (ex: 500 Ko) pour l'audio. Cela supprime définitivement le besoin d'un "Pacing" strict côté Python. Le serveur pourra envoyer l'audio à toute vitesse, la S3 stockera tout et le lira de manière ultra-fluide.
- **Stratégie mémoire :**
  - **PSRAM (8 Mo)** : Ring Buffers audio, Stacks FreeRTOS, Modèles WakeNet/MultiNet
  - **SRAM interne (512 Ko)** : Tampons DMA I2S, Calculs matriciels AEC
- **Nouveau Visage (Face Renderer) :** 
  - La mémoire Flash de 16 Mo nous permettra de stocker de vraies images (BMP/JPEG) ou des *spritesheets* pour les yeux et les expressions.
  - La PSRAM servira de *Frame Buffer* (mémoire vidéo). L'écran TFT utilisera la DMA pour piocher directement dans la PSRAM.
  - Résultat : Des animations de visage ultra-modernes, fluides (60 FPS), avec des clignements d'yeux naturels et des transitions douces, sans **jamais** bloquer l'audio ou le réseau.

## 3. Étapes d'implémentation

### Phase 1 : Migration Hardware (Immédiat) ✅
1. **✅ Nouveau câblage :** `CABLAGE_S3.md` créé avec GPIO Matrix optimisé pour 2 breadboards.
2. **✅ PlatformIO :** `platformio.ini` migré vers `esp32-s3-devkitc-1`, PSRAM OPI, Flash 16MB.
3. **✅ Re-mapping des Pins :** Toutes les broches audio (I2S), écran (SPI), IR remappées.
4. **✅ Suppression tactile/SD :** Code nettoyé, montage simplifié.

### Phase 2 : Nouvelles Fonctionnalités Firmware (Après réception carte)
1. **Intégration du Volume & IR :** Coder la logique du gain audio contrôlé par l'IR.
2. **Intégration ESP-SR :** Implémenter le Wake-Word, l'AEC et le mode Sleep.
3. **Ring Buffers PSRAM :** Migrer les buffers audio vers la PSRAM.
4. **Suppression de Silero VAD & RMS (Python) :**
   - Grâce à l'AEC et au VAD (Voice Activity Detection) matériel de la S3, l'ESP32 ne transmettra l'audio au serveur **que** lorsqu'une voix humaine est détectée.
   - **Bénéfice :** Réduction massive de la charge CPU du PC et économie de bande passante WiFi.
5. **Ajustement Python :** Simplifier `server.py` pour qu'il ne fasse plus de "pacing".

### Phase 3 : Visage DMA & Sprites
1. **Visage DMA :** Nouveau renderer avec spritesheets en PSRAM, DMA vers écran 60 FPS.

## 4. Évolutions Logicielles (Serveur Python) — "Zéro impact VRAM"

Puisque nous sommes contraints par les 6 Go de VRAM (déjà bien occupés par Gemma et Whisper), ces améliorations logicielles utiliseront des techniques intelligentes pour ne consommer **aucune VRAM supplémentaire** et très peu de RAM système.

### A. Mémoire à Long Terme (Système de "Souvenirs")
- Au lieu de charger un lourd modèle de base de données vectorielle, nous allons utiliser **Gemma elle-même** !
- On lui apprendra à utiliser un "outil" interne. S'il détecte une information importante (ex: *"Je travaille sur des plaques à essai"*), le LLM génèrera une commande spéciale en arrière-plan `SAVE_MEMORY("Le robot est sur des plaques à essai")`. 
- Ces mémoires sont stockées dans un simple fichier texte/JSON. 
- Au démarrage de la conversation, le serveur injecte silencieusement ces mémoires dans le prompt : *"Voici ce que tu sais de cet utilisateur : ..."*.

### B. Changement de Personnalité à la volée (via Infrarouge)
- Le serveur Python possèdera un dictionnaire de Profils.
- En appuyant sur les touches de la télécommande (ex: 1, 2, 3), l'ESP32 enverra un code au serveur.
- Le serveur remplacera instantanément le "System Prompt" (pour changer l'attitude de Gemma) et le fichier de voix "Kokoro" (pour changer le ton/genre). 
- *Impact RAM : Zéro.*

### C. Exécution d'Actions Locales (Tool Calling)
- Gemma 4 E2B est experte en appels de fonctions. On peut lui donner la capacité de déclencher des scripts Bash, de lire l'heure, ou de chercher une définition sur Wikipédia sans faire exploser la mémoire.

### D. Mode "Thinking" & Mini-Jeux (Chain of Thought)
- **Le Problème :** Les LLMs luttent contre les contraintes négatives (ex: "Ni oui ni non").
- **La Solution :** Activer un mode "Thinking" temporaire. Le serveur injecte un champ `"thought"` dans le JSON que Gemma doit remplir *avant* de parler (`"speech"`). En réfléchissant à la règle avant de formuler sa réponse, le robot devient imbattable.
- **Gestion de la Latence :** Ce mode rajoute ~1s de latence. Le serveur enverra donc un statut `{"status": "thinking"}` à l'ESP32 pour qu'il affiche un regard "pensif" ou joue un petit son ("Hmm..."). Ce mode ne s'activera que si l'utilisateur lance un jeu ou pose une énigme complexe. Le reste du temps, le robot restera en mode "Bavardage instantané" (500ms).

## User Review Required
> [!IMPORTANT]
> Phase 1 (Migration Hardware) : ✅ TERMINÉE
> Ce document est la feuille de route officielle pour les Phases 2 et 3 à venir.
