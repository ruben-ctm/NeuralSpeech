#include "menu.h"
#include "ecrans.h"
#include "vocal.h"

// Options du menu (correspond aux commandes vocales)
static const char* MENU_LABELS[] = { "Pokedex", "Combat" };
static const int   MENU_N = 2;

// ── Affichage de base ─────────────────────────────────────────────────────
static void menu_dessiner(int selection, bool ecoute) {
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);

  // Titre
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(12, 2);
  ecran_rouge.print("NeuralDex");
  ecran_rouge.drawLine(0, 20, 127, 20, SSD1306_WHITE);

  ecran_rouge.setTextSize(1);
  for (int i = 0; i < MENU_N; i++) {
    int y = 27 + i * 16;
    if (i == selection) {
      ecran_rouge.fillRect(0, y - 2, 128, 13, SSD1306_WHITE);
      ecran_rouge.setTextColor(SSD1306_BLACK);
    } else {
      ecran_rouge.setTextColor(SSD1306_WHITE);
    }
    ecran_rouge.setCursor(10, y);
    ecran_rouge.print("> ");
    ecran_rouge.print(MENU_LABELS[i]);
  }
  rouge_envoyer();

  // Écran bleu : statut écoute
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

  // Indicateur écoute : barre en bas, y=53 height=9 → y+h=62 < 64
  ecran_bleu.drawRect(0, 53, 128, 9, SSD1306_WHITE);
  if (ecoute) {
    ecran_bleu.fillRect(1, 54, 126, 7, SSD1306_WHITE);
    ecran_bleu.setTextColor(SSD1306_BLACK);
    ecran_bleu.setCursor(22, 55);
    ecran_bleu.print("Ecoute active...");
  } else {
    ecran_bleu.setTextColor(SSD1306_WHITE);
    ecran_bleu.setCursor(38, 55);
    ecran_bleu.print("Analyse...");
  }
  bleu_envoyer();
}

// ── Animation sélection : la barre clignote avant de lancer ──────────────
static void menu_animer_selection(int selection) {
  // Barre qui clignote 2x
  for (int i = 0; i < 2; i++) {
    menu_dessiner(selection, false);
    delay(160);
    // Barre invisible
    ecran_rouge.clearDisplay();
    ecran_rouge.setTextColor(SSD1306_WHITE);
    ecran_rouge.setTextSize(2);
    ecran_rouge.setCursor(12, 2);
    ecran_rouge.print("NeuralDex");
    ecran_rouge.drawLine(0, 20, 127, 20, SSD1306_WHITE);
    ecran_rouge.setTextSize(1);
    for (int j = 0; j < MENU_N; j++) {
      ecran_rouge.setTextColor(SSD1306_WHITE);
      ecran_rouge.setCursor(10, 27 + j * 16);
      ecran_rouge.print("> ");
      ecran_rouge.print(MENU_LABELS[j]);
    }
    rouge_envoyer();
    delay(160);
  }

  // 1 clignotement LED verte de confirmation
  digitalWrite(7, HIGH);  // PIN_LED_V
  delay(120);
  digitalWrite(7, LOW);
}

// ── Affichage résultat vocal sur écran bleu ──────────────────────────────
static void menu_afficher_resultat(const char* mot, float conf) {
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);
  ecran_bleu.setCursor(10, 8);
  ecran_bleu.print("Reconnu :");
  ecran_bleu.setTextSize(2);
  ecran_bleu.setCursor(10, 22);
  ecran_bleu.print(mot);
  ecran_bleu.setTextSize(1);
  ecran_bleu.setCursor(10, 46);
  ecran_bleu.print("Confiance: ");
  ecran_bleu.print((int)(conf * 100));
  ecran_bleu.print("%");
  bleu_envoyer();
}

// ── API publique ──────────────────────────────────────────────────────────
void menu_afficher() {
  menu_dessiner(0, true);
}

// Boucle VAD intégrée au menu : retourne l'état cible quand un mot est dit
Etat menu_boucle_vad() {
  // Afficher menu en mode écoute
  menu_dessiner(0, true);

  while (true) {
    // Afficher "Analyse..." sur bleu pendant que VAD écoute
    menu_dessiner(0, true);

    int cmd = vocal_attendre_voix(300);

    if (cmd == CMD_AUCUNE) continue;

    digitalWrite(7, LOW);  // LED verte off

    if (cmd == CMD_POKEDEX) {
      menu_animer_selection(0);
      return ETAT_POKEDEX;
    }
    if (cmd == CMD_COMBAT) {
      menu_animer_selection(1);
      return ETAT_COMBAT;
    }
    // CMD_MENU = annuler, on reste
  }
}

// Conservé pour compatibilité (non utilisé dans le nouveau flow)
Etat menu_update(int commande, bool bouton) {
  (void)commande; (void)bouton;
  return ETAT_MENU;
}
