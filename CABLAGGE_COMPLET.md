# Guide de Câblage Définitif - Robot IA (Architecture v3.0)

Ce document est la référence matérielle absolue pour le projet.

## 1. Plan des GPIO (ESP32)

| Composant | Signal | Pin ESP32 | Note |
| :--- | :--- | :--- | :--- |
| **Bus SPI** | SCK / MOSI / MISO | **18 / 23 / 19** | Bus partagé (Écran, SD, Touch) |
| **Écran TFT** | CS / DC / RST | **14 / 21 / 4** | RST sur Pin 4 (Source de stabilité) |
| **Carte SD** | CS | **5** | |
| **Tactile** | CS | **13** | |
| **Haut-Parleur**| DIN / LRC / BCLK | **12 / 26 / 27** | DIN déplacé sur 12 (évite I2C SDA) |
| | **SD_MODE (Mute)**| **15** | **LOW = Muet**, HIGH = ON |
| **Microphone** | WS / SCK / SD | **25 / 32 / 33** | L/R relié au GND |

## 2. Bus SPI Partagé
Reliez les broches suivantes en parallèle (en "bus") :
1.  **SCK (18)** -> SCK(Écran) + SCK(SD) + T_CLK(Tactile)
2.  **MOSI (23)** -> SDI(Écran) + MOSI(SD) + T_DIN(Tactile)
3.  **MISO (19)** -> SDO(SD) + T_DO(Tactile)

## 3. Filtrage et Alimentation (Crucial pour le son)
Pour éliminer le souffle et les craquements :
*   **Alimentation Ampli :** Condensateur **100µF** + **100nF** entre le VIN (5V) et le GND du MAX98357A.
*   **Alimentation Micro :** Condensateur **10µF** + **100nF** entre le VCC (3.3V) et le GND du INMP441.
*   **Gain :** Résistance de **10kΩ** entre la pin GAIN du haut-parleur et le GND.
*   **Mute :** Résistance de **390kΩ** entre la pin SD du haut-parleur et le 5V (Pull-up de sécurité).

## 4. Format des Fichiers SD
*   **UI :** Dossier `/ui/` contenant des fichiers `.gif` optimisés (320x240, 16 couleurs).
*   **Musique :** Dossier `/music/` contenant des fichiers `.mp3`.

---
*Fichier généré le 23 Avril 2026 - Certifié Stable*
