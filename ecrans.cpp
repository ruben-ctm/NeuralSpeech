#include "ecrans.h"

Adafruit_SSD1306 ecran_rouge(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire,  OLED_RESET);
Adafruit_SSD1306 ecran_bleu (SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

static bool g_bleu_ok = false;

// Envoie les 1024 octets du framebuffer en hex sur le port série
// Format : [FB_ROUGE]<2048 hex chars>\n  ou  [FB_BLEU]<2048 hex chars>\n
static void serial_send_framebuffer(Adafruit_SSD1306& ecran, const char* name) {
  Serial.print("[FB_");
  Serial.print(name);
  Serial.print("]");
  uint8_t* buf = ecran.getBuffer();
  for (uint16_t i = 0; i < 1024; i++) {
    uint8_t b = buf[i];
    if (b < 0x10) Serial.print('0');
    Serial.print(b, HEX);
  }
  Serial.println();
}

void ecrans_init() {
  if (!ecran_rouge.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Ecran rouge FAILED");
    while (true);
  }
  g_bleu_ok = ecran_bleu.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!g_bleu_ok) {
    Serial.println("Ecran bleu FAILED");
    Serial.println("Mode degrade: rouge uniquement");
  }
  ecran_rouge.clearDisplay();
  ecran_rouge.display();
  if (g_bleu_ok) {
    ecran_bleu.clearDisplay();
    ecran_bleu.display();
  }
}

// ── Écran rouge ────────────────────────────────────

void rouge_effacer() {
  ecran_rouge.clearDisplay();
}

void rouge_titre(const char* ligne1, const char* ligne2) {
  ecran_rouge.clearDisplay();
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(2);
  ecran_rouge.setCursor(0, 5);
  ecran_rouge.println(ligne1);
  if (ligne2) {
    ecran_rouge.setTextSize(1);
    ecran_rouge.setCursor(0, 28);
    ecran_rouge.println(ligne2);
  }
  rouge_envoyer();
}

void rouge_texte(uint8_t col, uint8_t ligne, uint8_t taille, const char* texte) {
  ecran_rouge.setTextColor(SSD1306_WHITE);
  ecran_rouge.setTextSize(taille);
  ecran_rouge.setCursor(col, ligne);
  ecran_rouge.print(texte);
}

void rouge_barre(uint8_t x, uint8_t y, uint8_t largeur, uint8_t hauteur, uint8_t valeur, uint8_t max_val) {
  ecran_rouge.drawRect(x, y, largeur, hauteur, SSD1306_WHITE);
  uint8_t rempli = (uint8_t)((long)valeur * (largeur - 2) / max_val);
  if (rempli > 0)
    ecran_rouge.fillRect(x + 1, y + 1, rempli, hauteur - 2, SSD1306_WHITE);
}

void rouge_envoyer() {
  ecran_rouge.display();
  serial_send_framebuffer(ecran_rouge, "ROUGE");
}

// ── Écran bleu ─────────────────────────────────────

void bleu_effacer() {
  if (!g_bleu_ok) return;
  ecran_bleu.clearDisplay();
}

void bleu_texte(uint8_t col, uint8_t ligne, uint8_t taille, const char* texte) {
  if (!g_bleu_ok) return;
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(taille);
  ecran_bleu.setCursor(col, ligne);
  ecran_bleu.print(texte);
}

static void bleu_barre(uint8_t x, uint8_t y, uint8_t largeur, uint8_t hauteur, uint8_t valeur, uint8_t max_val) {
  if (!g_bleu_ok) return;
  ecran_bleu.drawRect(x, y, largeur, hauteur, SSD1306_WHITE);
  if (max_val == 0) return;
  uint8_t rempli = (uint8_t)((long)valeur * (largeur - 2) / max_val);
  if (rempli > 0)
    ecran_bleu.fillRect(x + 1, y + 1, rempli, hauteur - 2, SSD1306_WHITE);
}

void bleu_stats(uint8_t hp, uint8_t atk, uint8_t def, uint8_t spd) {
  if (!g_bleu_ok) return;
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);

  const char* labels[] = {"HP ", "ATK", "DEF", "SPD"};
  uint8_t     vals[]   = {hp, atk, def, spd};
  uint8_t     maxs[]   = {160, 134, 130, 130};

  char buf[16];
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t y = 4 + i * 15;
    ecran_bleu.setCursor(0, y);
    ecran_bleu.print(labels[i]);
    ecran_bleu.print(":");
    sprintf(buf, "%3d", vals[i]);
    ecran_bleu.setCursor(96, y);
    ecran_bleu.print(buf);
    bleu_barre(28, y, 64, 7, vals[i], maxs[i]);
  }
  bleu_envoyer();
}

void bleu_combat(uint8_t hp1, uint8_t hp1_max, uint8_t hp2, uint8_t hp2_max, const char* msg) {
  if (!g_bleu_ok) return;
  ecran_bleu.clearDisplay();
  ecran_bleu.setTextColor(SSD1306_WHITE);
  ecran_bleu.setTextSize(1);

  ecran_bleu.setCursor(0, 2);
  ecran_bleu.print("P1:");
  bleu_barre(20, 2, 100, 7, hp1, hp1_max);
  char buf[8];
  sprintf(buf, "%d", hp1);
  ecran_bleu.setCursor(100, 2);
  ecran_bleu.print(buf);

  ecran_bleu.setCursor(0, 18);
  ecran_bleu.print("P2:");
  bleu_barre(20, 18, 100, 7, hp2, hp2_max);
  sprintf(buf, "%d", hp2);
  ecran_bleu.setCursor(100, 18);
  ecran_bleu.print(buf);

  ecran_bleu.setCursor(0, 38);
  ecran_bleu.print(msg);

  bleu_envoyer();
}

void bleu_envoyer() {
  if (!g_bleu_ok) return;
  ecran_bleu.display();
  serial_send_framebuffer(ecran_bleu, "BLEU");
}
