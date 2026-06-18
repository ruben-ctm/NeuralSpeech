#ifndef VOCAL_H
#define VOCAL_H

#include <Arduino.h>

// ── Commandes reconnues ────────────────────────────
#define CMD_AUCUNE    -1
#define CMD_MENU       0   // "menu"
#define CMD_COMBAT     1   // "combat"
#define CMD_POKEDEX    2   // "pokedex" → ouvre le Pokédex
#define CMD_SUIVANT    3
#define CMD_PRECEDENT  4

// Commandes noms Pokémon (index dans pokedex_data)
#define CMD_POKEMON_0  10

// ── Déclarations externes (définies dans NeuralSpeech.ino) ────────────────
extern const char*     NN_LABELS[];
extern const uint16_t  NN_LAYERS[];

// ── API ────────────────────────────────────────────
void  vocal_init(uint8_t pin_micro);
int   vocal_capturer();                        // enregistre 1s + MFCC + inférence
int   vocal_attendre_voix(uint32_t timeout_ms); // VAD : attend une voix puis infère
float vocal_derniere_confiance();              // confiance du dernier résultat (0-1)

#endif
