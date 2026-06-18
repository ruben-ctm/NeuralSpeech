# 🎙️ NeuralDex — Pokédex & Combat Vocal Embarqué

[![Arduino](https://img.shields.io/badge/Arduino-Due-00979D.svg)](https://docs.arduino.cc/hardware/due/)
[![C++](https://img.shields.io/badge/C++-embedded-blue.svg)](https://isocpp.org/)
[![No ML framework](https://img.shields.io/badge/ML-from%20scratch-orange.svg)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## 📋 Description

Pokédex interactif et système de combat 1v1 entièrement piloté par la voix, sur Arduino Due. Le pipeline de reconnaissance vocale — acquisition, filtrage, MFCC, inférence du réseau de neurones — est codé en C++ bas niveau directement sur les registres du microcontrôleur, sans aucune librairie ML embarquée.

**Stack**: C++ (registres SAM3X8E), Python (entraînement hors-ligne), Arduino Due, 2× OLED SSD1306

## 🎯 Features

- ✅ Reconnaissance vocale on-device, sans réseau ni cloud
- ✅ VAD (Voice Activity Detection) continu en énergie RMS
- ✅ Pipeline DSP fait main : FIR, FFT (Cooley-Tukey), MFCC, banc de filtres Mel
- ✅ Inférence MLP `[793→64→32→3]` en pur C++, poids en `PROGMEM`
- ✅ Double affichage OLED synchronisé (navigation + stats en parallèle)
- ✅ Machine à états complète (menu, pokédex, combat)
- ✅ Debug framebuffer via port série (visualisation PC en temps réel)

## 🏗️ Architecture

```
├── NeuralSpeech.ino     # Boot, état global, lecture bouton (clics/appui long)
├── vocal.cpp/h          # Pipeline complet : ADC → FIR → MFCC → inférence MLP, VAD
├── nn_weights.h         # Poids du réseau entraîné (export PROGMEM)
├── ecrans.cpp/h         # Pilotage 2 écrans OLED + envoi framebuffer série
├── menu.cpp/h           # Menu principal en écoute vocale continue
├── pokedex.cpp/h        # Navigation base de données Pokémon
├── combat.cpp/h         # Machine à états du combat 1v1
├── pokemon_data.h       # Base de données statique (stats, types)
└── types.h              # États globaux partagés
```

## 🚀 Quick Start

### Prerequisites

- Arduino IDE 1.8+ ou PlatformIO
- Arduino Due
- Librairies `Adafruit_GFX` et `Adafruit_SSD1306`

### Installation

```bash
# Clone le repo
git clone https://github.com/ruben-ctm/neuraldex.git
cd neuraldex

# Ouvrir dans Arduino IDE
# Sélectionner la carte : Arduino Due (Programming Port)
```

### Câblage

```
Micro électret  → A0
Écran OLED #1   → I2C bus Wire   (0x3C)
Écran OLED #2   → I2C bus Wire1  (0x3C)
Bouton          → D2 (INPUT_PULLUP)
LED rouge       → D6
LED verte       → D7
```

### Flash & lancer

```bash
# Compiler et uploader depuis Arduino IDE (Ctrl+U)
# ou en CLI avec arduino-cli
arduino-cli compile --fqbn arduino:sam:arduino_due_x neuraldex
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:sam:arduino_due_x neuraldex
```

### Debug visuel (PC)

Le firmware envoie les deux framebuffers OLED en hex sur le port série (`115200 baud`), format `[FB_ROUGE]<hex>` / `[FB_BLEU]<hex>` — utile pour visualiser les écrans sans matériel physique branché en continu.

## 🎙️ Pipeline de reconnaissance vocale

| Étage | Détail |
|---|---|
| Acquisition | ADC 32 kHz via timer `TC0`, interruption matérielle (registres SAM3X8E) |
| Filtrage | FIR passe-bas 45 coefficients, coupure 4 kHz, appliqué dans l'ISR |
| Downsampling | 32 kHz → 8 kHz (ratio 4:1) |
| MFCC | FFT 256 pts (Cooley-Tukey), 26 filtres Mel, DCT orthogonale → 13 coeffs × 61 frames |
| Normalisation | Standardisation moyenne/écart-type par coefficient |
| Inférence | MLP `[793→64→32→3]`, activations `tanh`, sortie `softmax`, poids en Flash |

Mots reconnus : `"combat"`, `"pokedex"`, `"menu"`. Seuil de confiance minimum : 70 %.

## 🧪 Entraînement du modèle

Les poids dans `nn_weights.h` sont générés hors-ligne par un script Python (`train.py`, non inclus dans ce repo) à partir d'enregistrements vocaux collectés directement sur le hardware — les MFCC sont calculés sur l'Arduino pendant la collecte pour garantir l'alignement exact entre données d'entraînement et pipeline d'inférence embarqué.

```bash
# Process général (script externe)
python train.py --data recordings/ --epochs 200
python export_weights.py --model model.pt --output nn_weights.h
```

## 📈 Résultats

| Metric | Value |
|--------|-------|
| Précision classification | 97,5%+ |
| Classes | 3 (combat, menu, pokedex) |
| Taille réseau | ~52k paramètres |
| Inférence | quelques ms (Cortex-M3, sans FPU dédiée ML) |
| RAM utilisée | poids en Flash (PROGMEM), pas en RAM |

## 📝 TODO / Roadmap

- [ ] Ajouter de nouvelles commandes vocales (noms de Pokémon)
- [ ] Réduire le délai de VAD pour une détection plus réactive
- [ ] Export du script d'entraînement Python dans le repo
- [ ] Support d'un micro plus directif pour réduire le seuil VAD en environnement bruyant

## 🤝 Contributing

Projet personnel à but d'apprentissage — suggestions bienvenues via issues.

## 📄 License

MIT License - voir [LICENSE](LICENSE)

## 👤 Auteur

**Ruben Combe-Tamain**
- GitHub: [@ruben-ctm](https://github.com/ruben-ctm)
- LinkedIn: [Ruben Combe-Tamain](https://www.linkedin.com/in/rubencombe-tamain/)

---
⭐ Star ce projet si tu le trouves utile !
