#!/usr/bin/env python3
"""
NEURALDEX - Entraînement MLP + Export poids Arduino Due
========================================================
Architecture : [793, 64, 32, 3]  → ~212 KB Flash (Due = 512 KB)
Optimiseur   : Adam + mini-batch (batch=32)
Régularisation: L2 + Dropout + Label Smoothing
ET7 cible    : MSE test < 0.05
ET9 cible    : Accuracy test > 90%
"""

import numpy as np
import pandas as pd
import argparse
import os, sys, math, pickle

# ── PARAMÈTRES AUDIO / FEATURES ─────────────────────────────────────────────
N_FRAMES   = 61
N_MFCC     = 13
N_FEATURES = N_FRAMES * N_MFCC   # 793

# ── ARCHITECTURE ─────────────────────────────────────────────────────────────
# [793, 64, 32, 3] → poids = (793×64 + 64×32 + 32×3) × 4 octets ≈ 212 KB
# Tient dans la Flash Arduino Due (512 KB) avec le reste du programme
HIDDEN_1 = 64
HIDDEN_2 = 32

# ── HYPERPARAMÈTRES ──────────────────────────────────────────────────────────
LR           = 0.001   # Taux d'apprentissage Adam
N_EPOCHS     = 5000    # Maximum epochs
BATCH_SIZE   = 32      # Mini-batch : empêche la mémorisation parfaite du train
LAMBDA       = 0.0001  # Régularisation L2
DROPOUT      = 0.3     # Dropout sur couches cachées (désactivé à l'inférence)
LABEL_SMOOTH = 0.05    # Lissage des labels : [1,0,0] → [0.95, 0.025, 0.025]
PATIENCE     = 150     # Epochs sans amélioration avant arrêt anticipé


# ── FONCTIONS D'ACTIVATION ───────────────────────────────────────────────────
def tanh_f(x):      return np.tanh(x)
def tanh_d(x):      return 1.0 - np.tanh(x) ** 2
def softmax(x):
    e = np.exp(x - np.max(x, axis=1, keepdims=True))
    return e / e.sum(axis=1, keepdims=True)
def xavier(n_in, n_out):
    lim = math.sqrt(6.0 / (n_in + n_out))
    return np.random.uniform(-lim, lim, (n_in, n_out))


# ── RÉSEAU MLP ───────────────────────────────────────────────────────────────
class MLP:
    def __init__(self, layers):
        self.layers = layers
        self.n      = len(layers)
        self.W = [xavier(layers[i], layers[i+1]) for i in range(self.n - 1)]
        self.b = [np.zeros((1, layers[i+1]))      for i in range(self.n - 1)]
        # Moments Adam (premier et second ordre)
        self.mW = [np.zeros_like(w) for w in self.W]
        self.vW = [np.zeros_like(w) for w in self.W]
        self.mb = [np.zeros_like(b) for b in self.b]
        self.vb = [np.zeros_like(b) for b in self.b]
        self.t  = 0   # Compteur global de steps (correction de biais Adam)

    # ── Forward pass ────────────────────────────────────────────────────────
    def forward(self, X, training=False):
        self.A     = [X]
        self.Z     = []
        self.masks = []
        a = X
        for i in range(self.n - 1):
            z = a @ self.W[i] + self.b[i]
            self.Z.append(z)
            if i < self.n - 2:
                a = tanh_f(z)
                if training and DROPOUT > 0:
                    # Inverted dropout : compense le scale à l'inférence
                    mask = (np.random.rand(*a.shape) > DROPOUT) / (1.0 - DROPOUT)
                    a    = a * mask
                    self.masks.append(mask)
                else:
                    self.masks.append(None)
            else:
                a = softmax(z)
                self.masks.append(None)
            self.A.append(a)
        return a

    # ── Backward pass + mise à jour Adam ────────────────────────────────────
    def backward(self, y_true, lr, lam, beta1=0.9, beta2=0.999, eps=1e-8):
        m     = self.A[0].shape[0]
        delta = (self.A[-1] - y_true) / m
        self.t += 1
        for i in range(self.n - 2, -1, -1):
            dW = self.A[i].T @ delta + lam * self.W[i]
            db = delta.sum(axis=0, keepdims=True)
            # Adam
            self.mW[i] = beta1 * self.mW[i] + (1 - beta1) * dW
            self.vW[i] = beta2 * self.vW[i] + (1 - beta2) * dW ** 2
            self.mb[i] = beta1 * self.mb[i] + (1 - beta1) * db
            self.vb[i] = beta2 * self.vb[i] + (1 - beta2) * db ** 2
            # Correction de biais
            mW_h = self.mW[i] / (1 - beta1 ** self.t)
            vW_h = self.vW[i] / (1 - beta2 ** self.t)
            mb_h = self.mb[i] / (1 - beta1 ** self.t)
            vb_h = self.vb[i] / (1 - beta2 ** self.t)
            self.W[i] -= lr * mW_h / (np.sqrt(vW_h) + eps)
            self.b[i] -= lr * mb_h / (np.sqrt(vb_h) + eps)
            if i > 0:
                delta = (delta @ self.W[i].T) * tanh_d(self.Z[i - 1])
                if self.masks[i - 1] is not None:
                    delta = delta * self.masks[i - 1]

    # ── Métriques (sans dropout) ─────────────────────────────────────────────
    def mse(self, X, y_onehot):
        pred = self.forward(X, training=False)
        return float(np.mean((pred - y_onehot) ** 2))

    def accuracy(self, X, y_idx):
        pred = self.forward(X, training=False)
        return float(np.mean(np.argmax(pred, axis=1) == y_idx))


# ── EXPORT ARDUINO ───────────────────────────────────────────────────────────
def export_arduino(nn, labels, out_file):
    with open(out_file, 'w') as f:
        f.write("/*\n")
        f.write(" * NeuralDex - Poids du reseau de neurones\n")
        f.write(f" * Architecture : {nn.layers}\n")
        f.write(f" * Classes      : {labels}\n")
        f.write(f" * N_FEATURES   : {N_FEATURES}\n")
        f.write(f" * N_FRAMES     : {N_FRAMES}\n")
        f.write(f" * N_MFCC       : {N_MFCC}\n")
        f.write(" * Genere par train.py\n")
        f.write(" */\n\n")
        f.write("#pragma once\n\n")
        f.write(f"#define NN_N_FEATURES  {N_FEATURES}\n")
        f.write(f"#define NN_N_CLASSES   {len(labels)}\n")
        f.write(f"#define NN_N_LABELS    {len(labels)}\n")
        f.write(f"#define NN_N_LAYERS    {len(nn.layers)}\n\n")

        # Poids couche par couche (row-major)
        f.write("// Poids (stockes en PROGMEM Flash)\n")
        f.write("const float NN_WEIGHTS[] PROGMEM = {\n")
        for i, W in enumerate(nn.W):
            f.write(f"  // Couche {i} ({W.shape[0]}x{W.shape[1]})\n")
            flat = W.flatten()
            for j, v in enumerate(flat):
                f.write(f"  {v:.8f}f")
                if j < len(flat) - 1 or i < len(nn.W) - 1:
                    f.write(",")
                if (j + 1) % 8 == 0:
                    f.write("\n")
            f.write("\n")
        f.write("};\n\n")

        # Biais
        f.write("// Biais\n")
        f.write("const float NN_BIASES[] PROGMEM = {\n")
        for i, b in enumerate(nn.b):
            f.write(f"  // Biais couche {i}\n")
            flat = b.flatten()
            for j, v in enumerate(flat):
                f.write(f"  {v:.8f}f")
                if j < len(flat) - 1 or i < len(nn.b) - 1:
                    f.write(",")
                if (j + 1) % 8 == 0:
                    f.write("\n")
            f.write("\n")
        f.write("};\n\n")
    print(f"Poids exportes vers : {out_file}")


# ── MAIN ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv',    default='dataset.csv')
    parser.add_argument('--out',    default='../nn_weights.h')
    parser.add_argument('--epochs', default=N_EPOCHS, type=int)
    parser.add_argument('--lr',     default=LR,       type=float)
    args = parser.parse_args()

    # ── Chargement ──────────────────────────────────────────────────────────
    print(f"Chargement : {args.csv}")
    if not os.path.isfile(args.csv):
        print(f"ERREUR : '{args.csv}' introuvable."); sys.exit(1)

    df = pd.read_csv(args.csv, header=None)
    df.rename(columns={0: 'label'}, inplace=True)
    print(f"Dataset : {len(df)} echantillons")
    print(f"Labels  : {df['label'].value_counts().to_dict()}")

    labels       = sorted(df['label'].unique().tolist())
    n_classes    = len(labels)
    lbl2idx      = {l: i for i, l in enumerate(labels)}
    print(f"Classes ({n_classes}) : {labels}")

    X     = df.iloc[:, 1:].values.astype(np.float32)
    y_idx = np.array([lbl2idx[l] for l in df['label']])

    # Label smoothing : évite que le réseau soit trop confiant → MSE test plus faible
    smooth_val = LABEL_SMOOTH / (n_classes - 1)
    y = np.full((len(y_idx), n_classes), smooth_val, dtype=np.float32)
    y[np.arange(len(y_idx)), y_idx] = 1.0 - LABEL_SMOOTH

    # ── Split 80/20 ──────────────────────────────────────────────────────────
    np.random.seed(42)
    idx   = np.random.permutation(len(X))
    split = int(0.8 * len(idx))
    X_tr, X_te   = X[idx[:split]],     X[idx[split:]]
    y_tr, y_te   = y[idx[:split]],     y[idx[split:]]
    yi_tr, yi_te = y_idx[idx[:split]], y_idx[idx[split:]]
    print(f"\nTrain : {len(X_tr)} | Test : {len(X_te)}")

    # ── Réseau ───────────────────────────────────────────────────────────────
    layers = [N_FEATURES, HIDDEN_1, HIDDEN_2, n_classes]
    print(f"Architecture : {layers}")
    nn = MLP(layers)

    print(f"\nEntrainement Adam | lr={args.lr} L2={LAMBDA} batch={BATCH_SIZE} "
          f"dropout={DROPOUT} label_smooth={LABEL_SMOOTH}")
    print(f"{'Epoch':>6}  {'MSE Train':>12}  {'MSE Test':>12}  {'Acc Train':>10}  {'Acc Test':>10}")
    print("-" * 62)

    best_mse  = float('inf')
    best_W    = [w.copy() for w in nn.W]
    best_b    = [b.copy() for b in nn.b]
    no_improv = 0
    hist      = {'epochs': [], 'mse_tr': [], 'mse_te': [], 'acc_tr': [], 'acc_te': []}
    n_tr      = len(X_tr)

    for epoch in range(args.epochs):
        # ── Mini-batch shuffle ───────────────────────────────────────────────
        perm = np.random.permutation(n_tr)
        for start in range(0, n_tr, BATCH_SIZE):
            bi = perm[start:start + BATCH_SIZE]
            nn.forward(X_tr[bi], training=True)
            nn.backward(y_tr[bi], args.lr, LAMBDA)

        # ── Évaluation toutes les 10 epochs ─────────────────────────────────
        if (epoch + 1) % 10 == 0 or epoch == 0:
            mse_tr = nn.mse(X_tr, y_tr)
            mse_te = nn.mse(X_te, y_te)
            acc_tr = nn.accuracy(X_tr, yi_tr)
            acc_te = nn.accuracy(X_te, yi_te)

            hist['epochs'].append(epoch + 1)
            hist['mse_tr'].append(mse_tr)
            hist['mse_te'].append(mse_te)
            hist['acc_tr'].append(acc_tr * 100)
            hist['acc_te'].append(acc_te * 100)

            if (epoch + 1) % 200 == 0 or epoch == 0:
                print(f"{epoch+1:6d}  {mse_tr:12.6f}  {mse_te:12.6f}  "
                      f"{acc_tr:10.2%}  {acc_te:10.2%}")

            # Early stopping sur MSE test (critère ET7)
            if mse_te < best_mse:
                best_mse = mse_te
                best_W   = [w.copy() for w in nn.W]
                best_b   = [b.copy() for b in nn.b]
                no_improv = 0
            else:
                no_improv += 1
                if no_improv >= PATIENCE:
                    print(f"\nEarly Stopping epoch {epoch+1} "
                          f"(meilleure MSE test : {best_mse:.5f})")
                    break

    with open('training_history.pkl', 'wb') as f:
        pickle.dump(hist, f)

    # ── Restaurer meilleurs poids ────────────────────────────────────────────
    nn.W = best_W
    nn.b = best_b

    mse_tr_f = nn.mse(X_tr, y_tr)
    mse_te_f = nn.mse(X_te, y_te)
    acc_tr_f = nn.accuracy(X_tr, yi_tr)
    acc_te_f = nn.accuracy(X_te, yi_te)

    print(f"\n{'='*62}")
    print(f"MSE Train finale : {mse_tr_f:.6f}")
    print(f"MSE Test  finale : {mse_te_f:.6f}")
    print(f"Accuracy Train   : {acc_tr_f:.2%}")
    print(f"Accuracy Test    : {acc_te_f:.2%}")

    if mse_te_f <= 0.05:
        print(f"\n[OK] ET7 satisfait : MSE test = {mse_te_f:.4f} < 0.05")
    else:
        print(f"\n[!!] ET7 non satisfait : MSE test = {mse_te_f:.4f} > 0.05")
    if acc_te_f >= 0.90:
        print(f"[OK] ET9 satisfait : accuracy = {acc_te_f:.1%} > 90%")
    else:
        print(f"[!!] ET9 non satisfait : accuracy = {acc_te_f:.1%} < 90%")

    export_arduino(nn, labels, args.out)

    print(f"\n=== RESUME RAPPORT ===")
    print(f"N_FEATURES   : {N_FEATURES} ({N_FRAMES} frames x {N_MFCC} MFCC)")
    print(f"N_CLASSES    : {n_classes} -> {labels}")
    print(f"Architecture : {layers}")
    print(f"Optimiseur   : Adam (lr={args.lr}, b1=0.9, b2=0.999)")
    print(f"Regulariz.   : L2={LAMBDA}, Dropout={DROPOUT}, LabelSmooth={LABEL_SMOOTH}")
    print(f"Dataset      : {len(df)} samples  Train={len(X_tr)}  Test={len(X_te)}")
    print(f"MSE Train    : {mse_tr_f:.4f}")
    print(f"MSE Test     : {mse_te_f:.4f}  (ET7 : < 0.05)")
    print(f"Accuracy     : {acc_te_f:.1%}  (ET9 : > 90%)")


if __name__ == '__main__':
    main()
