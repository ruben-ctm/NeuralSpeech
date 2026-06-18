/*
 * collect_serial_auto.ino
 * =======================
 * Collecte AUTOMATIQUE du dataset - 100 enregistrements par mot.
 * Pas besoin d'appuyer sur le bouton.
 *
 * UTILISATION :
 *   1. Téléverser ce sketch
 *   2. Ouvrir Serial Monitor à 115200 baud
 *   3. Taper : go:menu    → 100 enregistrements de "menu" automatiques
 *   4. Taper : go:combat  → 100 enregistrements de "combat"
 *   5. Taper : go:list    → 100 enregistrements de "list"
 *   6. Copier le Serial Monitor dans raw_output.txt
 *   7. python clean_serial.py → dataset.csv
 *
 * TIMING :
 *   - 1s d'enregistrement
 *   - 1.5s de pause (temps pour prononcer + délai)
 *   - = ~2.5s par enregistrement → 100x = ~4 minutes par mot
 *
 * LED rouge  = enregistrement en cours → PARLE MAINTENANT
 * LED verte  = pause → prépare le mot suivant
 *
 * STOP : taper 'stop' pour arrêter à tout moment
 */

#include <math.h>

// ─── PINS ────────────────────────────────────────────────────
#define PIN_BOUTON  2
#define PIN_LED_R   6
#define PIN_LED_V   7

// ─── PARAMÈTRES AUDIO ────────────────────────────────────────
#define FE_ACQ      32000
#define FE_MFCC     8000
#define RATIO_DOWN  4
#define N_SAMPLES   8000

// ─── FILTRE RIF PASSE-BAS Fc=4kHz Fe=32kHz ──────────────────
#define FIR_LEN 45
const int16_t fir_coeffs[FIR_LEN] = {
  -53, -87, -118, -134, -122, -74,  10, 118, 233, 330,
   385, 376,  288,  123, -98, -340, -557, -703, -737, -633,
  -386, -24,  388,  790, 1124, 1343, 1415, 1343, 1124,  790,
   388, -24, -386, -633, -737, -703, -557, -340,  -98,  123,
   288, 376,  385,  330,  233
};

// ─── PARAMÈTRES MFCC ─────────────────────────────────────────
#define FRAME_SIZE  256
#define HOP_SIZE    128
#define N_FRAMES    61
#define N_FFT       256
#define N_FFT_HALF  128
#define N_MELS      26
#define N_MFCC      13
#define PREEMPH     0.97f

// ─── COLLECTE AUTO ───────────────────────────────────────────
#define N_AUTO      100     // Nombre d'enregistrements automatiques
#define PAUSE_MS    1500    // Pause entre enregistrements (ms)

// ─── BUFFERS ─────────────────────────────────────────────────
uint16_t  fir_buf[FIR_LEN];
int16_t   audio_8k[N_SAMPLES];
float     mfcc_all[N_FRAMES][N_MFCC];

volatile uint32_t sample_count   = 0;
volatile uint32_t downsample_acc = 0;
volatile bool     recording      = false;
volatile bool     record_done    = false;

char  current_label[32] = "menu";
int   auto_count        = 0;    // Combien d'enregistrements faits
int   auto_total        = 0;    // Combien on veut faire (0 = pas en mode auto)
bool  stop_requested    = false;

// ─── BUFFERS MFCC ────────────────────────────────────────────
float fft_real[N_FFT];
float fft_imag[N_FFT];
float power_spec[N_FFT_HALF + 1];
float mel_energies[N_MELS];
float hamming_win[FRAME_SIZE];
float dct_table[N_MFCC][N_MELS];
float fft_cos_table[N_FFT_HALF];
float fft_sin_table[N_FFT_HALF];

struct MelBin { uint8_t k; float w; };
MelBin  mel_sparse[N_MELS][30];
uint8_t mel_len[N_MELS];

// ─── SETUP ADC ───────────────────────────────────────────────
void setupADC() {
  PMC->PMC_PCER1 |= PMC_PCER1_PID37;
  ADC->ADC_CR = ADC_CR_SWRST;
  ADC->ADC_MR =
      ADC_MR_TRGEN_DIS      |
      ADC_MR_LOWRES_BITS_12 |
      ADC_MR_PRESCAL(3)     |
      ADC_MR_STARTUP_SUT64  |
      ADC_MR_TRACKTIM(15)   |
      ADC_MR_SETTLING_AST3;
  ADC->ADC_CHER = ADC_CHER_CH7;
}

// ─── SETUP TIMER 32kHz ───────────────────────────────────────
void setupTimer() {
  PMC->PMC_PCER0 |= PMC_PCER0_PID27;
  TC0->TC_CHANNEL[0].TC_CMR =
      TC_CMR_TCCLKS_TIMER_CLOCK1 |
      TC_CMR_CPCTRG;
  TC0->TC_CHANNEL[0].TC_RC = 1312;
  TC0->TC_CHANNEL[0].TC_IER = TC_IER_CPCS;
  NVIC_EnableIRQ(TC0_IRQn);
  TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
}

void stopTimer() {
  TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKDIS;
  NVIC_DisableIRQ(TC0_IRQn);
}

// ─── ISR 32kHz ───────────────────────────────────────────────
void TC0_Handler() {
  uint32_t sr = TC0->TC_CHANNEL[0].TC_SR;
  (void)sr;
  if (!recording) return;

  ADC->ADC_CR = ADC_CR_START;
  while (!(ADC->ADC_ISR & ADC_ISR_EOC7));
  uint16_t raw = ADC->ADC_CDR[7];

  for (int i = FIR_LEN - 1; i > 0; i--) fir_buf[i] = fir_buf[i-1];
  fir_buf[0] = raw;
  int64_t acc = 0;
  for (int i = 0; i < FIR_LEN; i++) acc += (int64_t)fir_buf[i] * fir_coeffs[i];
  int16_t filtered = (int16_t)(acc >> 15);

  downsample_acc++;
  if (downsample_acc >= RATIO_DOWN) {
    downsample_acc = 0;
    audio_8k[sample_count++] = filtered;
    if (sample_count >= N_SAMPLES) {
      recording   = false;
      record_done = true;
    }
  }
}

// ─── TABLES MFCC ─────────────────────────────────────────────
void buildHamming() {
  for (int i = 0; i < FRAME_SIZE; i++)
    hamming_win[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (FRAME_SIZE - 1));
}

void buildFFTTable() {
  for (int i = 0; i < N_FFT_HALF; i++) {
    float angle = -2.0f * M_PI * i / N_FFT;
    fft_cos_table[i] = cosf(angle);
    fft_sin_table[i] = sinf(angle);
  }
}

void buildMelFilterbank() {
  auto hzToMel = [](float hz) { return 2595.0f * log10f(1.0f + hz / 700.0f); };
  auto melToHz = [](float mel) { return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f); };
  float mel_min = hzToMel(0.0f);
  float mel_max = hzToMel(FE_MFCC / 2.0f);
  int   bin_pts[N_MELS + 2];
  for (int i = 0; i < N_MELS + 2; i++) {
    float mel = mel_min + i * (mel_max - mel_min) / (N_MELS + 1);
    float hz  = melToHz(mel);
    bin_pts[i] = (int)floorf((N_FFT + 1) * hz / FE_MFCC);
    if (bin_pts[i] > N_FFT_HALF) bin_pts[i] = N_FFT_HALF;
  }
  for (int m = 0; m < N_MELS; m++) {
    mel_len[m] = 0;
    for (int k = bin_pts[m]; k < bin_pts[m+1] && k <= N_FFT_HALF; k++) {
      float w = (float)(k - bin_pts[m]) / (bin_pts[m+1] - bin_pts[m]);
      if (w > 0.001f && mel_len[m] < 30) { mel_sparse[m][mel_len[m]] = {(uint8_t)k, w}; mel_len[m]++; }
    }
    for (int k = bin_pts[m+1]; k <= bin_pts[m+2] && k <= N_FFT_HALF; k++) {
      float w = (float)(bin_pts[m+2] - k) / (bin_pts[m+2] - bin_pts[m+1]);
      if (w > 0.001f && mel_len[m] < 30) { mel_sparse[m][mel_len[m]] = {(uint8_t)k, w}; mel_len[m]++; }
    }
  }
}

void buildDCTTable() {
  for (int n = 0; n < N_MFCC; n++)
    for (int m = 0; m < N_MELS; m++)
      dct_table[n][m] = cosf(M_PI * n * (m + 0.5f) / N_MELS);
}

// ─── FFT ─────────────────────────────────────────────────────
void fft_fast(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t; t=re[i]; re[i]=re[j]; re[j]=t;
      t=im[i]; im[i]=im[j]; im[j]=t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    int half = len >> 1, step = n / len;
    for (int i = 0; i < n; i += len) {
      for (int j = 0; j < half; j++) {
        int ti = j * step;
        float wr = fft_cos_table[ti], wi = fft_sin_table[ti];
        float ur = re[i+j+half]*wr - im[i+j+half]*wi;
        float ui = re[i+j+half]*wi + im[i+j+half]*wr;
        re[i+j+half] = re[i+j]-ur; im[i+j+half] = im[i+j]-ui;
        re[i+j] += ur; im[i+j] += ui;
      }
    }
  }
}

// ─── MFCC ────────────────────────────────────────────────────
void computeMFCC(int frame_idx) {
  int start = frame_idx * HOP_SIZE;
  float prev = (start > 0) ? (float)audio_8k[start-1] : 0.0f;
  for (int i = 0; i < N_FFT; i++) {
    float s = (float)audio_8k[start + i];
    fft_real[i] = (s - PREEMPH * prev) * hamming_win[i];
    prev = s;
    fft_imag[i] = 0.0f;
  }
  fft_fast(fft_real, fft_imag, N_FFT);
  for (int k = 0; k <= N_FFT_HALF; k++)
    power_spec[k] = fft_real[k]*fft_real[k] + fft_imag[k]*fft_imag[k];
  for (int m = 0; m < N_MELS; m++) {
    float e = 0.0f;
    for (int b = 0; b < mel_len[m]; b++)
      e += mel_sparse[m][b].w * power_spec[mel_sparse[m][b].k];
    mel_energies[m] = logf(e + 1e-10f);
  }
  float scale0 = 1.0f / sqrtf(4.0f * N_MELS);
  float scaleN = 1.0f / sqrtf(2.0f * N_MELS);
  float lc = 22.0f / 2.0f;
  for (int n = 0; n < N_MFCC; n++) {
    float sum = 0.0f;
    for (int m = 0; m < N_MELS; m++)
      sum += mel_energies[m] * dct_table[n][m];
    float scale = (n == 0) ? scale0 : scaleN;
    float lift  = 1.0f + lc * sinf(M_PI * n / 22.0f);
    mfcc_all[frame_idx][n] = lift * scale * 2.0f * sum;
  }
}

void computeAllMFCC() {
  for (int f = 0; f < N_FRAMES; f++) computeMFCC(f);
}

void normalizeMFCC() {
  float mean[N_MFCC] = {0}, std_v[N_MFCC] = {0};
  for (int f = 0; f < N_FRAMES; f++)
    for (int c = 0; c < N_MFCC; c++) mean[c] += mfcc_all[f][c];
  for (int c = 0; c < N_MFCC; c++) mean[c] /= N_FRAMES;
  for (int f = 0; f < N_FRAMES; f++)
    for (int c = 0; c < N_MFCC; c++)
      std_v[c] += (mfcc_all[f][c]-mean[c])*(mfcc_all[f][c]-mean[c]);
  for (int c = 0; c < N_MFCC; c++) {
    std_v[c] = sqrtf(std_v[c] / N_FRAMES + 1e-10f);
    for (int f = 0; f < N_FRAMES; f++)
      mfcc_all[f][c] = (mfcc_all[f][c] - mean[c]) / std_v[c];
  }
}

void sendMFCC() {
  Serial.print(current_label);
  for (int f = 0; f < N_FRAMES; f++)
    for (int c = 0; c < N_MFCC; c++) {
      Serial.print(',');
      Serial.print(mfcc_all[f][c], 5);
    }
  Serial.println();
}

// ─── DÉMARRER UN ENREGISTREMENT ──────────────────────────────
void startRecording() {
  sample_count   = 0;
  downsample_acc = 0;
  record_done    = false;
  memset(fir_buf, 0, sizeof(fir_buf));
  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_V, LOW);
  recording = true;
  setupTimer();
}

// ─── SETUP ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOUTON, INPUT_PULLUP);
  pinMode(PIN_LED_R,  OUTPUT);
  pinMode(PIN_LED_V,  OUTPUT);
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_V, LOW);

  setupADC();
  buildHamming();
  buildFFTTable();
  buildMelFilterbank();
  buildDCTTable();

  Serial.println("=== NEURALDEX COLLECTE AUTO ===");
  Serial.println("# Commandes :");
  Serial.println("#   go:menu    → 100x 'menu' automatique");
  Serial.println("#   go:combat  → 100x 'combat' automatique");
  Serial.println("#   go:pokedex    → 100x 'pokedex' automatique");
  Serial.println("#   stop       → arrêter");
  Serial.println("# LED rouge = PARLE | LED verte = attends");
}

// ─── LOOP ────────────────────────────────────────────────────
void loop() {

  // ── Lecture commandes Serial ────────────────────────────────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "stop") {
      stop_requested = true;
      recording      = false;
      auto_total     = 0;
      auto_count     = 0;
      stopTimer();
      digitalWrite(PIN_LED_R, LOW);
      digitalWrite(PIN_LED_V, LOW);
      Serial.println("# STOP demandé.");

    } else if (cmd.startsWith("go:")) {
      String lbl = cmd.substring(3);
      lbl.trim();
      lbl.toCharArray(current_label, sizeof(current_label));
      auto_count     = 0;
      auto_total     = N_AUTO;
      stop_requested = false;

      Serial.print("# DEBUT collecte auto : ");
      Serial.print(auto_total);
      Serial.print("x '");
      Serial.print(current_label);
      Serial.println("'");
      Serial.println("# Prononce le mot à chaque LED rouge !");
      Serial.println("# Tape 'stop' pour arrêter.");

      // Countdown 3 secondes avant de commencer
      for (int i = 3; i > 0; i--) {
        Serial.print("# Début dans ");
        Serial.print(i);
        Serial.println("s...");
        delay(1000);
      }

      // Lancer le premier enregistrement
      startRecording();
    }
  }

  // ── Après enregistrement terminé ───────────────────────────
  if (record_done) {
    record_done = false;
    stopTimer();
    digitalWrite(PIN_LED_R, LOW);

    // Calculer et envoyer les MFCCs
    computeAllMFCC();
    normalizeMFCC();
    sendMFCC();

    auto_count++;
    Serial.print("# [");
    Serial.print(auto_count);
    Serial.print("/");
    Serial.print(auto_total);
    Serial.print("] '");
    Serial.print(current_label);
    Serial.println("' OK");

    if (auto_count >= auto_total || stop_requested) {
      // Collecte terminée
      auto_total = 0;
      auto_count = 0;
      Serial.print("# === COLLECTE TERMINEE : ");
      Serial.print(current_label);
      Serial.println(" ===");
      Serial.println("# Copie le Serial Monitor dans raw_output.txt");
      Serial.println("# puis lance : python clean_serial.py");

      // Clignoter les deux LEDs pour signaler la fin
      for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_LED_R, HIGH); digitalWrite(PIN_LED_V, HIGH); delay(150);
        digitalWrite(PIN_LED_R, LOW);  digitalWrite(PIN_LED_V, LOW);  delay(150);
      }

    } else {
      // Pause avant prochain enregistrement
      // LED verte pendant la pause
// Pas de pause LED verte - enchaîner directement
if (Serial.available()) {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd == "stop") stop_requested = true;
}

if (!stop_requested) {
  startRecording();
      } else {
        auto_total = 0;
        auto_count = 0;
        Serial.println("# Arrêté par stop.");
      }
    }
  }
}
