# NeuralDex

Pokédex interactif et système de combat piloté **entièrement par la voix**, tournant sur un Arduino Due. Aucune librairie de machine learning embarquée : l'acquisition audio, le filtrage du signal, l'extraction de features (MFCC) et l'inférence du réseau de neurones sont tous implémentés en C++ bas niveau, directement sur les registres du microcontrôleur.

Deux écrans OLED SSD1306 indépendants (I2C `Wire` / `Wire1`) affichent en parallèle deux vues complémentaires (navigation sur l'un, statistiques/contexte sur l'autre).

## Fonctionnement

- **Appui long** sur le bouton → déclenche l'écoute vocale (1 seconde d'enregistrement)
- **Mots reconnus** : `"combat"`, `"pokedex"`, `"menu"`
- **Bouton court** : 1 clic = suivant, 2 clics = précédent, 3 clics = valider (en combat)
- Le menu principal tourne en **VAD continu** (Voice Activity Detection) : il écoute en permanence et ne traite que les fenêtres où une voix est détectée, sans bouton à appuyer

## Pipeline de reconnaissance vocale

Tout le pipeline tourne on-device, sans dépendance réseau ni cloud :

1. **Acquisition** — ADC interne échantillonné à 32 kHz via les registres du timer `TC0` (SAM3X8E), déclenché par interruption matérielle
2. **Filtrage** — filtre RIF passe-bas (45 coefficients, coupure 4 kHz) appliqué en temps réel dans l'ISR, suivi d'un sous-échantillonnage 4:1 vers 8 kHz
3. **Extraction MFCC** — FFT 256 points (Cooley-Tukey, tables précalculées), banc de filtres Mel (26 filtres), DCT orthogonale → 13 coefficients MFCC par frame, sur 61 frames glissantes
4. **Normalisation** — standardisation (moyenne/écart-type) par coefficient sur l'ensemble des frames
5. **Inférence** — MLP `[793 → 64 → 32 → 3]`, poids entraînés hors-ligne puis exportés en `PROGMEM` (Flash) pour ne pas consommer de RAM, activations `tanh`, sortie `softmax`

La détection vocale (VAD) utilise l'énergie RMS d'une fenêtre de 128 ms pour démarrer l'enregistrement uniquement quand une voix est présente, en conservant le début du mot via un padding de silence calibré sur les conditions d'entraînement.

## Architecture du code

| Fichier | Rôle |
|---|---|
| `NeuralSpeech.ino` | Boot, état global, lecture bouton (clics multiples / appui long) |
| `vocal.cpp/h` | Pipeline complet : ADC, FIR, MFCC, inférence MLP, VAD |
| `nn_weights.h` | Poids du réseau entraîné (généré par un script d'entraînement externe) |
| `ecrans.cpp/h` | Pilotage des deux écrans OLED, envoi du framebuffer en série pour debug PC |
| `menu.cpp/h` | Menu principal en écoute vocale continue |
| `pokedex.cpp/h` | Navigation dans la base de données Pokémon |
| `combat.cpp/h` | Machine à états du combat 1v1 (sélection, tirage au sort, tours, dégâts) |
| `pokemon_data.h` | Base de données statique (stats, types) |
| `types.h` | États globaux partagés |

## Matériel

- Arduino Due (SAM3X8E, Cortex-M3)
- Microphone électret (entrée ADC `A0`)
- 2× écran OLED SSD1306 128×64 (I2C, un par bus `Wire`/`Wire1`)
- 1 bouton poussoir, 2 LEDs (rouge/verte)

## Pourquoi pas une lib MFCC/ML existante

L'objectif était de comprendre et maîtriser chaque étage du pipeline — de l'ADC à la classification — sans boîte noire, sur une cible aux ressources contraintes (pas de FPU dédiée pour le ML, RAM limitée). Le réseau est volontairement petit (793 entrées, ~52k paramètres) pour tenir en Flash et s'exécuter en quelques dizaines de millisecondes.
