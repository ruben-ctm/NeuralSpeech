#include "pokedex.h"
#include "ecrans.h"
#include "vocal.h"
#include "pokemon_data.h"

static uint8_t index_courant  = 0;
static bool    veut_quitter   = false;

static void pokedex_afficher() {
  const Pokemon& pk = pokedex_data[index_courant];

  // ── Écran rouge : nom, numéro, type ──────────────
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);

  // Numéro
  char buf[8];
  sprintf(buf, "#%03d", pk.numero);
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(0, 0);
  ecran_rouge.print(buf);

  // Index dans notre liste
  sprintf(buf, "%d/%d", index_courant + 1, NB_POKEMON);
  ecran_rouge.setCursor(90, 0);
  ecran_rouge.print(buf);

  // Nom
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(0, 14);
  ecran_rouge.print(pk.nom);

  // Type
  ecran_rouge.setTextSize(1);
  ecran_rouge.setCursor(0, 34);
  ecran_rouge.print("Type: ");
  ecran_rouge.print(pk.type);

  // Navigation hint
  ecran_rouge.setCursor(0, 50);
  ecran_rouge.print("<precedent / suivant>");

  rouge_envoyer();

  // ── Écran bleu : stats ───────────────────────────
  bleu_stats(pk.hp, pk.atk, pk.def, pk.spd);
}

void pokedex_init() {
  index_courant = 0;
  veut_quitter  = false;
  pokedex_afficher();
}

void pokedex_refresh() {
  pokedex_afficher();
}

void pokedex_update(int commande, bool bouton) {
  bool changed = false;

  if (commande == CMD_SUIVANT) {
    index_courant = (index_courant + 1) % NB_POKEMON;
    changed = true;
  } else if (commande == CMD_PRECEDENT) {
    index_courant = (index_courant == 0) ? NB_POKEMON - 1 : index_courant - 1;
    changed = true;
  }

  // Recherche par nom vocal
  for (uint8_t i = CMD_POKEMON_0; i < CMD_POKEMON_0 + NB_POKEMON; i++) {
    if (commande == i) {
      index_courant = i - CMD_POKEMON_0;
      changed = true;
      break;
    }
  }

  if (changed) {
    pokedex_afficher();
  }

  // Bouton long = quitter (détection appui prolongé)
  static unsigned long t_appui = 0;
  if (bouton && t_appui == 0) {
    t_appui = millis();
  } else if (!bouton && t_appui > 0) {
    if (millis() - t_appui > 800) {
      veut_quitter = true;
    }
    t_appui = 0;
  }
}

bool pokedex_veut_quitter() {
  if (veut_quitter) {
    veut_quitter = false;
    return true;
  }
  return false;
}
