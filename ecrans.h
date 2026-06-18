#ifndef ECRANS_H
#define ECRANS_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

extern Adafruit_SSD1306 ecran_rouge;
extern Adafruit_SSD1306 ecran_bleu;

void ecrans_init();

// Écran rouge — navigation / titre
void rouge_effacer();
void rouge_titre(const char* ligne1, const char* ligne2 = nullptr);
void rouge_texte(uint8_t col, uint8_t ligne, uint8_t taille, const char* texte);
void rouge_barre(uint8_t x, uint8_t y, uint8_t largeur, uint8_t hauteur, uint8_t valeur, uint8_t max_val);
void rouge_envoyer();   // display() + envoi framebuffer PC

// Écran bleu — infos / stats
void bleu_effacer();
void bleu_texte(uint8_t col, uint8_t ligne, uint8_t taille, const char* texte);
void bleu_stats(uint8_t hp, uint8_t atk, uint8_t def, uint8_t spd);
void bleu_combat(uint8_t hp1, uint8_t hp1_max, uint8_t hp2, uint8_t hp2_max, const char* msg);
void bleu_envoyer();    // display() + envoi framebuffer PC

#endif
