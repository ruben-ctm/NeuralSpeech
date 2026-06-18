/*
 * vocal.cpp — Pipeline reconnaissance vocale NeuralDex
 * =====================================================
 * FP1 : ADC 32kHz via Timer TC0 (registres SAM3X8E)
 * FP2 : Filtre RIF passe-bas Fc=4kHz + downsampling 8kHz
 * FP4 : MFCC (N_FFT=256, DCT ortho, 61 frames × 13 coeff)
 * FP5 : MLP [793→32→16→3] poids en PROGMEM
 */

#include "vocal.h"
#include "nn_weights.h"
#include <math.h>

// ─── PARAMÈTRES AUDIO ────────────────────────────────
#define FE_ACQ      32000
#define FE_MFCC     8000
#define RATIO_DOWN  4
#define N_SAMPLES   8000

// ─── FILTRE RIF PASSE-BAS Fc=4kHz Fe=32kHz ──────────
#define FIR_LEN 45
static const int16_t fir_coeffs[FIR_LEN] = {
  -53, -87, -118, -134, -122, -74,  10, 118, 233, 330,
   385, 376,  288,  123, -98, -340, -557, -703, -737, -633,
  -386, -24,  388,  790, 1124, 1343, 1415, 1343, 1124,  790,
   388, -24, -386, -633, -737, -703, -557, -340,  -98,  123,
   288, 376,  385,  330,  233
};

// ─── PARAMÈTRES MFCC ─────────────────────────────────
#define FRAME_SIZE  256
#define HOP_SIZE    128
#define N_FRAMES    61
#define N_FFT       256
#define N_FFT_HALF  128
#define N_MELS      26
#define N_MFCC      13
#define PREEMPH     0.97f

// ─── BUFFERS ─────────────────────────────────────────
static uint16_t  fir_buf[FIR_LEN];
static int16_t   audio_8k[N_SAMPLES];
static float     mfcc_all[N_FRAMES][N_MFCC];
static float     nn_input[NN_N_FEATURES];
static float     nn_out[NN_N_CLASSES];
static float     nn_a1[64];
static float     nn_a2[32];

// Volatile pour ISR
static volatile uint32_t sample_count   = 0;
static volatile uint32_t downsample_acc = 0;
static volatile bool     recording      = false;
static volatile bool     record_done    = false;

// ─── TABLES PRÉCALCULÉES ─────────────────────────────
static float fft_real[N_FFT];
static float fft_imag[N_FFT];
static float power_spec[N_FFT_HALF + 1];
static float mel_energies[N_MELS];
static float hamming_win[FRAME_SIZE];
static float dct_table[N_MFCC][N_MELS];
static float fft_cos_table[N_FFT_HALF];
static float fft_sin_table[N_FFT_HALF];

struct MelBin { uint8_t k; float w; };
static MelBin  mel_sparse[N_MELS][30];
static uint8_t mel_len[N_MELS];

// ─── SETUP ADC (registres SAM3X8E - TP0/TP1) ─────────
static void setupADC() {
  PMC->PMC_PCER1 |= PMC_PCER1_PID37;
  ADC->ADC_CR = ADC_CR_SWRST;
  ADC->ADC_MR =
      ADC_MR_TRGEN_DIS      |
      ADC_MR_LOWRES_BITS_12 |
      ADC_MR_PRESCAL(3)     |
      ADC_MR_STARTUP_SUT64  |
      ADC_MR_TRACKTIM(15)   |
      ADC_MR_SETTLING_AST3;
  ADC->ADC_CHER = ADC_CHER_CH7;  // A0 = canal 7
}

// ─── SETUP TIMER TC0 à 32kHz ─────────────────────────
static void setupTimer() {
  PMC->PMC_PCER0 |= PMC_PCER0_PID27;
  TC0->TC_CHANNEL[0].TC_CMR =
      TC_CMR_TCCLKS_TIMER_CLOCK1 |   // MCK/2 = 42MHz
      TC_CMR_CPCTRG;
  TC0->TC_CHANNEL[0].TC_RC = 1312;   // 42MHz / 32000 = 1312
  TC0->TC_CHANNEL[0].TC_IER = TC_IER_CPCS;
  NVIC_EnableIRQ(TC0_IRQn);
  TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
}

static void stopTimer() {
  TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKDIS;
  NVIC_DisableIRQ(TC0_IRQn);
}

// ─── ISR TC0 : ADC + FIR + downsampling ──────────────
void TC0_Handler() {
  uint32_t sr = TC0->TC_CHANNEL[0].TC_SR;
  (void)sr;
  if (!recording) return;

  ADC->ADC_CR = ADC_CR_START;
  while (!(ADC->ADC_ISR & ADC_ISR_EOC7));
  uint16_t raw = ADC->ADC_CDR[7];

  // FIR FIFO (TP8)
  for (int i = FIR_LEN - 1; i > 0; i--) fir_buf[i] = fir_buf[i-1];
  fir_buf[0] = raw;
  int64_t acc = 0;
  for (int i = 0; i < FIR_LEN; i++) acc += (int64_t)fir_buf[i] * fir_coeffs[i];
  int16_t filtered = (int16_t)(acc >> 15);

  // Downsampling 4:1
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

// ─── TABLES MFCC ─────────────────────────────────────
static void buildHamming() {
  for (int i = 0; i < FRAME_SIZE; i++)
    hamming_win[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (FRAME_SIZE - 1));
}

static void buildFFTTable() {
  for (int i = 0; i < N_FFT_HALF; i++) {
    float a = -2.0f * M_PI * i / N_FFT;
    fft_cos_table[i] = cosf(a);
    fft_sin_table[i] = sinf(a);
  }
}

static void buildMelFilterbank() {
  auto hzToMel = [](float hz){ return 2595.0f * log10f(1.0f + hz / 700.0f); };
  auto melToHz = [](float mel){ return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f); };
  float mel_min = hzToMel(0.0f), mel_max = hzToMel(FE_MFCC / 2.0f);
  int bin_pts[N_MELS + 2];
  for (int i = 0; i < N_MELS + 2; i++) {
    float mel = mel_min + i * (mel_max - mel_min) / (N_MELS + 1);
    bin_pts[i] = (int)floorf((N_FFT + 1) * melToHz(mel) / FE_MFCC);
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

static void buildDCTTable() {
  for (int n = 0; n < N_MFCC; n++)
    for (int m = 0; m < N_MELS; m++)
      dct_table[n][m] = cosf(M_PI * n * (m + 0.5f) / N_MELS);
}

// ─── FFT Cooley-Tukey (table lookup) ─────────────────
static void fft_fast(float* re, float* im, int n) {
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

// ─── MFCC ─────────────────────────────────────────────
static void computeMFCC(int frame_idx) {
  int start = frame_idx * HOP_SIZE;
  float prev = (start > 0) ? (float)audio_8k[start-1] : 0.0f;
  for (int i = 0; i < N_FFT; i++) {
    float s = (float)audio_8k[start + i];
    fft_real[i] = (s - PREEMPH * prev) * hamming_win[i];
    prev = s; fft_imag[i] = 0.0f;
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
    mfcc_all[frame_idx][n] = lift * scale * 2.0f * sum;  // facteur ×2 DCT ortho
  }
}

static void computeAllMFCC() {
  for (int f = 0; f < N_FRAMES; f++) computeMFCC(f);
}

static void normalizeMFCC() {
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

static void flattenMFCC() {
  int idx = 0;
  for (int f = 0; f < N_FRAMES; f++)
    for (int c = 0; c < N_MFCC; c++)
      nn_input[idx++] = mfcc_all[f][c];
}

// ─── INFÉRENCE MLP (poids PROGMEM) ───────────────────
static void nn_softmax(float* x, int n) {
  float mx = x[0];
  for (int i = 1; i < n; i++) if (x[i] > mx) mx = x[i];
  float sum = 0.0f;
  for (int i = 0; i < n; i++) { x[i] = expf(x[i]-mx); sum += x[i]; }
  for (int i = 0; i < n; i++) x[i] /= sum;
}

// Retourne l'index de la classe (0=combat, 1=list, 2=menu selon l'ordre d'entraînement)
static int inference() {
  int h1 = NN_LAYERS[1], h2 = NN_LAYERS[2], no = NN_N_CLASSES;

  for (int j = 0; j < h1; j++) {
    float z = pgm_read_float(&NN_BIASES[j]);
    for (int i = 0; i < NN_N_FEATURES; i++)
      z += nn_input[i] * pgm_read_float(&NN_WEIGHTS[i * h1 + j]);
    nn_a1[j] = tanhf(z);
  }
  int w2 = NN_N_FEATURES * h1;
  for (int j = 0; j < h2; j++) {
    float z = pgm_read_float(&NN_BIASES[h1 + j]);
    for (int i = 0; i < h1; i++)
      z += nn_a1[i] * pgm_read_float(&NN_WEIGHTS[w2 + i * h2 + j]);
    nn_a2[j] = tanhf(z);
  }
  int w3 = w2 + h1 * h2;
  for (int j = 0; j < no; j++) {
    float z = pgm_read_float(&NN_BIASES[h1 + h2 + j]);
    for (int i = 0; i < h2; i++)
      z += nn_a2[i] * pgm_read_float(&NN_WEIGHTS[w3 + i * no + j]);
    nn_out[j] = z;
  }
  nn_softmax(nn_out, no);
  int best = 0;
  for (int i = 1; i < no; i++) if (nn_out[i] > nn_out[best]) best = i;
  return best;
}

// ─── VAD (Voice Activity Detection) ──────────────────
// Fenêtre d'analyse : VAD_WIN samples à 8kHz (~128ms)
// Si énergie > seuil → continue l'enregistrement jusqu'à N_SAMPLES
// Les samples VAD sont conservés en début de buffer → pas de mot perdu
#define VAD_WIN  1024   // ~128ms à 8kHz (= 4 frames × 256)

static float vad_threshold = 250.0f;

// ─── API PUBLIQUE ─────────────────────────────────────

float vocal_derniere_confiance() {
  int best = 0;
  for (int i = 1; i < NN_N_CLASSES; i++) if (nn_out[i] > nn_out[best]) best = i;
  return nn_out[best];
}

void vocal_init(uint8_t pin_micro) {
  (void)pin_micro;
  setupADC();
  buildHamming();
  buildFFTTable();
  buildMelFilterbank();
  buildDCTTable();
}

// Enregistre 1 seconde, calcule MFCC, infère → retourne CMD_*
int vocal_capturer() {
  // Reset
  sample_count   = 0;
  downsample_acc = 0;
  record_done    = false;
  memset(fir_buf, 0, sizeof(fir_buf));

  // Démarrer acquisition
  recording = true;
  setupTimer();

  // Attendre fin enregistrement (1 seconde)
  while (!record_done) { /* spin */ }
  stopTimer();

  // Pipeline MFCC
  computeAllMFCC();
  normalizeMFCC();
  flattenMFCC();

  // Inférence
  int classe = inference();
  float conf = nn_out[classe];

  Serial.print("Vocal: ");
  Serial.print(NN_LABELS[classe]);
  Serial.print(" (");
  Serial.print(conf * 100.0f, 1);
  Serial.println("%)");

  // Mapper index du modèle vers CMD_*
  // Ordre alphabétique de l'entraînement : combat=0, menu=1, pokedex=2
  // NN_LABELS[0]="combat"  → CMD_COMBAT
  // NN_LABELS[1]="menu"    → CMD_MENU
  // NN_LABELS[2]="pokedex" → CMD_POKEDEX (pokedex ouvre le Pokédex)

  const char* label = NN_LABELS[classe];
  if      (strcmp(label, "combat")  == 0) return CMD_COMBAT;
  else if (strcmp(label, "pokedex") == 0) return CMD_POKEDEX;
  else if (strcmp(label, "menu")    == 0) return CMD_MENU;

  return CMD_AUCUNE;
}

// VAD continu : écoute par fenêtres de VAD_WIN samples.
// Quand voix détectée, CONTINUE l'enregistrement dans le même buffer
// jusqu'à N_SAMPLES → le début du mot est préservé.
int vocal_attendre_voix(uint32_t timeout_ms) {
  uint32_t debut = millis();

  while (millis() - debut < timeout_ms) {
    // Démarrer acquisition dans audio_8k depuis le début
    sample_count   = 0;
    downsample_acc = 0;
    record_done    = false;
    memset(fir_buf, 0, sizeof(fir_buf));

    recording = true;
    setupTimer();

    // Phase 1 : attendre VAD_WIN samples (fenêtre de détection)
    while (sample_count < VAD_WIN) { /* spin */ }

    // Calculer énergie RMS sur la fenêtre
    float energy = 0.0f;
    for (int i = 0; i < VAD_WIN; i++) {
      float s = (float)audio_8k[i];
      energy += s * s;
    }
    energy = sqrtf(energy / VAD_WIN);

    if (energy <= vad_threshold) {
      // Silence → arrêter et recommencer
      stopTimer();
      recording = false;
      continue;
    }

    // Voix détectée.
    // Le modèle a été entraîné avec bouton : la voix commence ~300ms après
    // le début du buffer. On simule ce délai en décalant les VAD_WIN samples
    // vers la position PAD et en remplissant le début avec des zéros.
    #define VAD_PAD  2400   // ~300ms à 8kHz
    // Décaler les samples capturés
    for (int i = VAD_WIN - 1; i >= 0; i--)
      audio_8k[VAD_PAD + i] = audio_8k[i];
    // Silence avant
    memset(audio_8k, 0, VAD_PAD * sizeof(int16_t));
    // sample_count repart depuis VAD_PAD + VAD_WIN
    sample_count = VAD_PAD + VAD_WIN;

    // Continuer l'enregistrement jusqu'à N_SAMPLES
    while (!record_done) { /* spin */ }
    stopTimer();

    // Pipeline MFCC + inférence (identique à vocal_capturer)
    computeAllMFCC();
    normalizeMFCC();
    flattenMFCC();

    int classe = inference();
    float conf = nn_out[classe];

    Serial.print("Vocal: ");
    Serial.print(NN_LABELS[classe]);
    Serial.print(" (");
    Serial.print(conf * 100.0f, 1);
    Serial.println("%)");

    if (conf < 0.70f) return CMD_AUCUNE;

    const char* label = NN_LABELS[classe];
    if      (strcmp(label, "combat")  == 0) return CMD_COMBAT;
    else if (strcmp(label, "pokedex") == 0) return CMD_POKEDEX;
    else if (strcmp(label, "menu")    == 0) return CMD_MENU;

    return CMD_AUCUNE;
  }

  return CMD_AUCUNE;
}
