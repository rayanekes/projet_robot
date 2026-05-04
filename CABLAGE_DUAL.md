# Guide de Câblage Définitif — Architecture Mono-MCU (S3 Only)

> [!IMPORTANT]
> **C'est LE document de référence.**
> L'architecture a été simplifiée : l'ESP32 V1 a été retirée. L'ESP32-S3 gère désormais tout (Audio In/Out, Écran, IR) grâce à la nouvelle API I2S native.
> Dernière mise à jour : 4 Mai 2026.

---

## ⚡ 1. Alimentation

Un seul câble USB alimente tout le système via l'**ESP32-S3**.

| Source | Destination | Rôle |
| :--- | :--- | :--- |
| **Powerbank 5V/2A** | **ESP32-S3 (USB)** | Alimentation unique |
| **Broche 3V3 de la S3** | **INMP441 (VCC)** | Alimentation micro |
| **Broche 5V de la S3** | **MAX98357A (VIN)** | Alimentation ampli |
| **Broche 3V3 de la S3** | **Écran ST7789 (VCC)** | Alimentation écran |

> [!WARNING]
> Utiliser un chargeur **5V 2A minimum**. L'ampli audio peut consommer beaucoup de courant sur les basses.

> [!TIP]
> Placer le condensateur **1000µF 16V** entre le 5V et le GND près du MAX98357A.

---

## 🔊 2. Ampli Audio MAX98357A (I2S_NUM_1 sur S3)

*Directement piloté par l'ESP32-S3.*

```text
MAX98357A      ESP32-S3
─────────      ────────
5V     ────→   5V (VBUS)   + 1000µF + 100nF entre VIN et GND
GND    ────→   GND
BCLK   ────→   GPIO 17
LRC    ────→   GPIO 18
DIN    ────→   GPIO 21
SD     ────→   100kΩ → 5V  (active le mode Mono Mix)
GAIN   ────→   10kΩ → GND  (gain 12dB, idéal pour la voix)
```

> [!NOTE]
> **Baffle 6W 8Ω** : Le firmware applique automatiquement un scaling à **60% d'amplitude** pour éviter le clipping.

---

## 🎤 3. Microphone INMP441 (I2S_NUM_0 sur S3)

```text
INMP441        ESP32-S3
───────        ────────
VCC    ────→   3V3
GND    ────→   GND
SCK    ────→   GPIO 4     (I2S Bit Clock RX)
WS     ────→   GPIO 5     (I2S Word Select RX)
SD     ────→   GPIO 6     (I2S Data In)
L/R    ────→   GND        (sélection canal gauche)
```

---

## 🖥️ 4. Écran TFT ST7789 (SPI FSPI)

```text
ST7789         ESP32-S3
──────         ────────
VCC    ────→   3V3
GND    ────→   GND
SCL    ────→   GPIO 12    (SPI Clock — FSPI natif)
SDA    ────→   GPIO 11    (SPI MOSI — FSPI natif)
CS     ────→   GPIO 10    (Chip Select)
DC     ────→   GPIO 9     (Data/Command)
RST    ────→   GPIO 13    (Reset)
BLK    ────→   GPIO 14    (Backlight — PWM)
```

---

## 📡 5. Récepteur Infrarouge IR1838B

```text
IR1838B        ESP32-S3
───────        ────────
VCC    ────→   3V3
GND    ────→   GND
OUT    ────→   GPIO 16    (Signal IR démodulé)
```

---

## 📋 6. Résumé GPIO — ESP32-S3

```text
Micro INMP441   : GPIO 4, 5, 6
Écran ST7789    : GPIO 9, 10, 11, 12, 13, 14
IR Récepteur    : GPIO 16
Ampli MAX98357A : GPIO 17, 18, 21
─────────────────────────────────────────────────────────────
Total           : 13 GPIO utilisés / 26 disponibles
Réserve         : 13 GPIO libres
```

### ⚠️ GPIO Interdits sur N16R8 (Octal SPI)

```text
❌ GPIO 26-37    : Bus Octal SPI Flash/PSRAM (physiquement occupés)
```

---

## 📐 7. Layout Physique — 2 Breadboards

```text
┌─────────────────────────────────────────────────────────────┐
│                      BREADBOARD 1 (Audio + S3)               │
│                                                              │
│  ┌───────────┐                                               │
│  │ INMP441   │←── 3 fils signal (GPIO 4, 5, 6)              │
│  │ (Micro)   │                                               │
│  └───────────┘                                               │
│                    ┌──────────────────────┐                   │
│                    │    ESP32-S3 N16R8    │                   │
│                    │    (YD Board)        │                   │
│                    └──────────────────────┘                   │
│                              │                                │
│                              │ I2S (GPIO 17, 18, 21)         │
│  ┌───────────┐               │                                │
│  │ MAX98357A │←──────────────┘                                │
│  │ → HP 8Ω   │                                               │
│  └───────────┘                                               │
└─────────────────────────────────────────────────────────────┘
                              │
                    nappe SPI (5 fils + RST + BL)
                              │
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                    BREADBOARD 2 (Affichage)                   │
│                                                              │
│            ┌─────────────────────┐                           │
│            │     ÉCRAN ST7789    │                           │
│            └─────────────────────┘                           │
│                                                              │
│            ┌───────────┐                                     │
│            │ IR1838B   │←── 1 fil signal (GPIO 16)           │
│            └───────────┘                                     │
└─────────────────────────────────────────────────────────────┘
```
