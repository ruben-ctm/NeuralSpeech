/*
 * NeuralDex — Arduino Due
 * ========================
 * Appui long  = reconnaissance vocale
 *   "combat"  → clignotement → ouvre 1v1 directement
 *   "pokedex" → clignotement → anime combat puis Pokédex
 *   "menu"    → retour menu depuis n'importe où
 *
 * Bouton court :
 *   1 clic  = suivant
 *   2 clics = précédent
 *   3 clics = valider (confirme le Pokémon en combat)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "types.h"
#include "pokemon_data.h"
#include "ecrans.h"
#include "menu.h"
#include "pokedex.h"
#include "combat.h"
#include "vocal.h"
#include "nn_weights.h"


// Définitions des labels et architecture du réseau neural
const char* NN_LABELS[3] = {
  "combat",
  "menu",
  "pokedex",
};

const uint16_t NN_LAYERS[4] = {793, 64, 32, 3};

#define PIN_BOUTON  2
#define PIN_LED_R   6
#define PIN_LED_V   7
#define PIN_MICRO  A0

#define FENETRE_MULTI_MS  400
#define DUREE_LONG_MS     800

static uint8_t  nb_clics       = 0;
static uint32_t t_dernier_clic = 0;
static uint32_t t_appui_debut  = 0;
static bool     bouton_tenu    = false;
static bool     long_emis      = false;

static int lire_bouton() {
  bool physique = (digitalRead(PIN_BOUTON) == LOW);
  uint32_t now  = millis();

  if (physique && !bouton_tenu) {
    t_appui_debut = now;
    bouton_tenu   = true;
    long_emis     = false;
  }
  if (physique && bouton_tenu && !long_emis) {
    if (now - t_appui_debut >= DUREE_LONG_MS) {
      long_emis = true;
      nb_clics  = 0;
      return -1;
    }
  }
  if (!physique && bouton_tenu) {
    bouton_tenu = false;
    if (!long_emis) {
      nb_clics++;
      t_dernier_clic = now;
    }
  }
  if (nb_clics > 0 && !bouton_tenu && (now - t_dernier_clic > FENETRE_MULTI_MS)) {
    uint8_t c = nb_clics;
    nb_clics = 0;
    return c;
  }
  return 0;
}

// Affichage écran rouge "Parle!" + écran bleu liste des mots disponibles
static void afficher_ecoute() {
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(15, 8);
  ecran_rouge.print("Parle !");
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(5, 42);
  ecran_rouge.print("1 seconde...");
  rouge_envoyer();

  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);
  ecran_bleu.setCursor(0, 2);
  ecran_bleu.print("Commandes vocales:");
  ecran_bleu.setCursor(4, 16);
  ecran_bleu.print("\"Pokedex\"");
  ecran_bleu.setCursor(4, 28);
  ecran_bleu.print("\"Combat\"");
  ecran_bleu.setCursor(4, 40);
  ecran_bleu.print("\"Menu\"");
  bleu_envoyer();
}

// Clignotement de confirmation vocale + affichage résultat sur écran bleu
static void clignote_confirmation(int cmd) {
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(2);
  ecran_bleu.setCursor(10, 10);
  if      (cmd == CMD_COMBAT)  ecran_bleu.print("COMBAT !");
  else if (cmd == CMD_POKEDEX) ecran_bleu.print("POKEDEX !");
  else if (cmd == CMD_MENU)    ecran_bleu.print("MENU !");
  ecran_bleu.setTextSize(1);
  ecran_bleu.setCursor(20, 40);
  ecran_bleu.print("Reconnaissance OK");
  bleu_envoyer();

  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_V, HIGH);
    delay(80);
    digitalWrite(PIN_LED_V, LOW);
    delay(80);
  }
}

Etat etatActuel = ETAT_MENU;

void setup() {
  Serial.begin(115200);
  Serial.println("NeuralDex boot OK !");

  pinMode(PIN_BOUTON, INPUT_PULLUP);
  pinMode(PIN_LED_R,  OUTPUT);
  pinMode(PIN_LED_V,  OUTPUT);

  Wire.begin();
  Wire1.begin();

  ecrans_init();
  vocal_init(PIN_MICRO);
  menu_afficher();
}

void loop() {
  int clics = lire_bouton();

  // ── MENU : VAD continu, tout est vocal ──────────────────────────────────
  if (etatActuel == ETAT_MENU) {
    Etat suivant = menu_boucle_vad();   // bloque jusqu'à mot reconnu
    if (suivant == ETAT_POKEDEX) {
      etatActuel = ETAT_POKEDEX;
      pokedex_init();
    } else if (suivant == ETAT_COMBAT) {
      etatActuel = ETAT_COMBAT;
      combat_init();
    }
    return;
  }

  // ── POKEDEX ───────────────────────────────────────────────────────────────
  if (etatActuel == ETAT_POKEDEX) {
    if (clics == 1) pokedex_update(CMD_SUIVANT,   false);
    if (clics == 2) pokedex_update(CMD_PRECEDENT, false);

    if (clics == -1) {
      afficher_ecoute();
      digitalWrite(PIN_LED_R, HIGH);
      int cmd = vocal_capturer();
      digitalWrite(PIN_LED_R, LOW);
      clignote_confirmation(cmd);
      if      (cmd == CMD_MENU)    { etatActuel = ETAT_MENU;    return; }
      else if (cmd == CMD_COMBAT)  { etatActuel = ETAT_COMBAT;  combat_init();  return; }
      else                          pokedex_refresh();
    }

    if (pokedex_veut_quitter()) etatActuel = ETAT_MENU;
    delay(20);
    return;
  }

  // ── COMBAT ────────────────────────────────────────────────────────────────
  if (etatActuel == ETAT_COMBAT) {
    combat_update(
      clics == 1 ? CMD_SUIVANT   :
      clics == 2 ? CMD_PRECEDENT : CMD_AUCUNE,
      clics == 3
    );

    if (clics == -1) {
      afficher_ecoute();
      digitalWrite(PIN_LED_R, HIGH);
      int cmd = vocal_capturer();
      digitalWrite(PIN_LED_R, LOW);
      clignote_confirmation(cmd);
      if      (cmd == CMD_MENU)    { etatActuel = ETAT_MENU;    return; }
      else if (cmd == CMD_POKEDEX) { etatActuel = ETAT_POKEDEX; pokedex_init(); return; }
      else                          combat_refresh();
    }

    if (combat_termine()) { delay(3000); etatActuel = ETAT_MENU; }
    delay(20);
  }
}
