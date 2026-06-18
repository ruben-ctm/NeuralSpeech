#include "combat.h"
#include "ecrans.h"
#include "vocal.h"
#include "pokemon_data.h"

typedef enum {
  COMBAT_CHOIX_P1,
  COMBAT_CHOIX_P2,
  COMBAT_CONFIRMATION,
  COMBAT_PILE_FACE,
  COMBAT_EN_COURS,
  COMBAT_FINI
} EtatCombat;

static EtatCombat etat_combat;
static uint8_t    idx_p1, idx_p2;
static int16_t    hp_p1,  hp_p2;
static uint8_t    tour;
static uint8_t    selection_idx;
static bool       is_termine;
static uint32_t   t_dernier_coup;

#define DELAI_COUP_MS  1200
#define PIN_LED_R  6
#define PIN_LED_V  7

// ── Dégâts ─────────────────────────────────────────
static uint8_t calculer_degats(uint8_t atk, uint8_t def) {
  long dmg = ((long)atk * 10) / (def / 2 + 1);
  dmg += random(-3, 4);
  if (dmg < 1)  dmg = 1;
  if (dmg > 99) dmg = 99;
  return (uint8_t)dmg;
}

// ── Affichage sélection ────────────────────────────
static void afficher_selection(uint8_t joueur) {
  const Pokemon& pk = pokedex_data[selection_idx];

  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(0, 0);
  ecran_rouge.print("Joueur ");
  ecran_rouge.print(joueur);
  ecran_rouge.print(" - choisir:");
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(0, 14);
  ecran_rouge.print(pk.nom);
  ecran_rouge.setTextSize(1);
  char buf[16];
  sprintf(buf, "#%03d | %s", pk.numero, pk.type);
  ecran_rouge.setCursor(0, 34);
  ecran_rouge.print(buf);
  ecran_rouge.setCursor(0, 50);
  ecran_rouge.print("<precedent / suivant>");
  rouge_envoyer();

  bleu_stats(pk.hp, pk.atk, pk.def, pk.spd);
}

// ── Affichage pile ou face ─────────────────────────
static void afficher_pile_face(uint8_t gagnant) {
  // gagnant=0 → P1 commence, gagnant=1 → P2 commence
  const char* nom   = pokedex_data[gagnant == 0 ? idx_p1 : idx_p2].nom;
  char label[3];
  sprintf(label, "P%d", gagnant + 1);

  // Bleu figé sur "Qui commence ?", rouge fait l'animation P1/P2
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);
  ecran_bleu.setCursor(18, 20);
  ecran_bleu.print("Qui commence ?");
  bleu_envoyer();

  for (int anim = 0; anim < 6; anim++) {
    const char* mot = (anim % 2 == 0) ? "P1" : "P2";

    ecran_rouge.clearDisplay();
    ecran_rouge.setTextColor(SSD1306_WHITE);
    ecran_rouge.setTextSize(3);
    ecran_rouge.setCursor(34, 22);
    ecran_rouge.print(mot);
    rouge_envoyer();

    delay(300);
  }

  // Résultat : label (P1/P2) à gauche, texte à droite
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(3);
  ecran_rouge.setCursor(28, 16);
  ecran_rouge.print(label);
  rouge_envoyer();

  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);
  ecran_bleu.setCursor(0, 10);
  ecran_bleu.print(nom);
  ecran_bleu.setCursor(0, 28);
  ecran_bleu.print("commence");
  ecran_bleu.setCursor(0, 40);
  ecran_bleu.print("le combat !");
  bleu_envoyer();
}

// ── Affichage confirmation (P1 vs P2) ───────────────
static void afficher_confirmation() {
  const Pokemon& p1 = pokedex_data[idx_p1];
  const Pokemon& p2 = pokedex_data[idx_p2];
  char buf[20];

  // Écran rouge : les deux combattants face à face
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(22, 0);
  ecran_rouge.print("-- Combat --");
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(0, 10);
  ecran_rouge.print(p1.nom);
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(50, 28);
  ecran_rouge.print("vs");
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(0, 36);
  ecran_rouge.print(p2.nom);
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(14, 56);
  rouge_envoyer();

  // Écran bleu : stats des deux Pokémon
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);

  ecran_bleu.setCursor(0, 0);
  ecran_bleu.print("P1: ");
  ecran_bleu.print(p1.nom);
  sprintf(buf, "HP:%d A:%d D:%d", p1.hp, p1.atk, p1.def);
  ecran_bleu.setCursor(0, 10);
  ecran_bleu.print(buf);

  ecran_bleu.drawLine(0, 22, 127, 22, SSD1306_WHITE);

  ecran_bleu.setCursor(0, 26);
  ecran_bleu.print("P2: ");
  ecran_bleu.print(p2.nom);
  sprintf(buf, "HP:%d A:%d D:%d", p2.hp, p2.atk, p2.def);
  ecran_bleu.setCursor(0, 36);
  ecran_bleu.print(buf);

  ecran_bleu.setCursor(0, 52);
  ecran_bleu.print("Appuyer pour lancer");
  bleu_envoyer();
}

// ── Affichage combat ───────────────────────────────
static void afficher_combat(const char* msg) {
  const Pokemon& p1 = pokedex_data[idx_p1];
  const Pokemon& p2 = pokedex_data[idx_p2];

  uint8_t hp1 = (hp_p1 > 0) ? (uint8_t)hp_p1 : 0;
  uint8_t hp2 = (hp_p2 > 0) ? (uint8_t)hp_p2 : 0;

  // ── Écran rouge : noms + message uniquement ────────────
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(1);

  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(4, 22);
  ecran_rouge.print(tour == 0 ? "P1" : "P2");
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(32, 28);
  ecran_rouge.print("attaque !");
  rouge_envoyer();

  // ── Écran bleu : HP chiffrés (complémentaire) ──────────
  char buf[24];
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);

  // P1
  ecran_bleu.setCursor(0, 0);
  ecran_bleu.print("P1 ");
  ecran_bleu.print(p1.nom);
  sprintf(buf, "%d/%d", hp1, p1.hp);
  ecran_bleu.setCursor(90, 0);
  ecran_bleu.print(buf);
  ecran_bleu.drawRect(0, 10, 126, 6, SSD1306_WHITE);
  if (p1.hp > 0 && hp1 > 0) {
    uint8_t w = (uint8_t)((long)hp1 * 124 / p1.hp);
    ecran_bleu.fillRect(1, 11, w, 4, SSD1306_WHITE);
  }

  // P2
  ecran_bleu.setCursor(0, 20);
  ecran_bleu.print("P2 ");
  ecran_bleu.print(p2.nom);
  sprintf(buf, "%d/%d", hp2, p2.hp);
  ecran_bleu.setCursor(90, 20);
  ecran_bleu.print(buf);
  ecran_bleu.drawRect(0, 30, 126, 6, SSD1306_WHITE);
  if (p2.hp > 0 && hp2 > 0) {
    uint8_t w = (uint8_t)((long)hp2 * 124 / p2.hp);
    ecran_bleu.fillRect(1, 31, w, 4, SSD1306_WHITE);
  }

  // Dernier coup
  ecran_bleu.setCursor(0, 44);
  ecran_bleu.print(">> ");
  ecran_bleu.print(msg);
  bleu_envoyer();
}

// ── Affichage victoire ─────────────────────────────
static void afficher_victoire(uint8_t gagnant) {
  const char* nom = pokedex_data[gagnant == 0 ? idx_p1 : idx_p2].nom;
  char player[3];
  sprintf(player, "P%d", gagnant + 1);

  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(0, 5);
  ecran_rouge.print("Victoire!");
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(0, 30);
  ecran_rouge.print(player);
  ecran_rouge.print(" - ");
  ecran_rouge.print(nom);
  ecran_rouge.setCursor(0, 44);
  ecran_rouge.print("gagne le combat!");
  rouge_envoyer();

  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(2);
  ecran_bleu.setCursor(20, 20);
  ecran_bleu.print("GG !");
  bleu_envoyer();
}

// ── API publique ────────────────────────────────────
void combat_init() {
  etat_combat   = COMBAT_CHOIX_P1;
  selection_idx = 0;
  is_termine    = false;
  tour          = 0;
  t_dernier_coup = 0;
  afficher_selection(1);
}

void combat_update(int commande, bool bouton) {
  switch (etat_combat) {

    case COMBAT_CHOIX_P1:
      if (commande == CMD_SUIVANT) {
        selection_idx = (selection_idx + 1) % NB_POKEMON;
        afficher_selection(1);
      } else if (commande == CMD_PRECEDENT) {
        selection_idx = (selection_idx == 0) ? NB_POKEMON - 1 : selection_idx - 1;
        afficher_selection(1);
      }
      if (bouton) {
        idx_p1 = selection_idx;
        hp_p1  = pokedex_data[idx_p1].hp;
        selection_idx = 0;
        delay(200);
        etat_combat = COMBAT_CHOIX_P2;
        afficher_selection(2);
      }
      break;

    case COMBAT_CHOIX_P2:
      if (commande == CMD_SUIVANT) {
        selection_idx = (selection_idx + 1) % NB_POKEMON;
        afficher_selection(2);
      } else if (commande == CMD_PRECEDENT) {
        selection_idx = (selection_idx == 0) ? NB_POKEMON - 1 : selection_idx - 1;
        afficher_selection(2);
      }
      if (bouton) {
        idx_p2 = selection_idx;
        hp_p2  = pokedex_data[idx_p2].hp;
        delay(200);
        etat_combat = COMBAT_CONFIRMATION;
        afficher_confirmation();
      }
      break;

    case COMBAT_CONFIRMATION:
      if (bouton || commande == CMD_SUIVANT) {
        delay(200);
        etat_combat = COMBAT_PILE_FACE;
        uint8_t pf  = (uint8_t)random(0, 2);
        tour = pf;
        afficher_pile_face(pf);
        delay(1500);
        etat_combat    = COMBAT_EN_COURS;
        t_dernier_coup = millis();
        afficher_combat("Go !");
      }
      break;

    case COMBAT_EN_COURS:
      {
        uint32_t now = millis();
        if (now - t_dernier_coup < DELAI_COUP_MS) break;
        t_dernier_coup = now;

        char msg[24];
        uint8_t dmg;

        if (tour == 0) {
          dmg    = calculer_degats(pokedex_data[idx_p1].atk, pokedex_data[idx_p2].def);
          hp_p2 -= dmg;
          sprintf(msg, "%s: -%d PV", pokedex_data[idx_p1].nom, dmg);
          digitalWrite(PIN_LED_R, HIGH); delay(80); digitalWrite(PIN_LED_R, LOW);
          if (hp_p2 <= 0) {
            hp_p2 = 0;
            afficher_victoire(0);
            etat_combat = COMBAT_FINI;
            is_termine  = true;
            return;
          }
          tour = 1;
        } else {
          dmg    = calculer_degats(pokedex_data[idx_p2].atk, pokedex_data[idx_p1].def);
          hp_p1 -= dmg;
          sprintf(msg, "%s: -%d PV", pokedex_data[idx_p2].nom, dmg);
          digitalWrite(PIN_LED_R, HIGH); delay(80); digitalWrite(PIN_LED_R, LOW);
          if (hp_p1 <= 0) {
            hp_p1 = 0;
            afficher_victoire(1);
            etat_combat = COMBAT_FINI;
            is_termine  = true;
            return;
          }
          tour = 0;
        }
        afficher_combat(msg);
      }
      break;

    case COMBAT_PILE_FACE:
    case COMBAT_FINI:
      break;
  }
}

void combat_refresh() {
  switch (etat_combat) {
    case COMBAT_CHOIX_P1: afficher_selection(1);   break;
    case COMBAT_CHOIX_P2: afficher_selection(2);   break;
    case COMBAT_EN_COURS: afficher_combat("..."); break;
    default: break;
  }
}

bool combat_termine() { return is_termine; }
