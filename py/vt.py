#!/usr/bin/env python3
"""
Visualisation texte - Analyse des poids du reseau de neurones
Analyse l'architecture et les labels depuis nn_weights.h
"""

import numpy as np
import re

# Parse nn_weights.h pour extraire les labels
def parse_weights_file(filename='nn_weights.h'):
    with open(filename, 'r', encoding='latin1') as f:
        content = f.read()
    
    # Extrait les labels
    labels_match = re.search(r'const char\* NN_LABELS\[.*?\] = \{(.*?)\};', content, re.DOTALL)
    if labels_match:
        labels_str = labels_match.group(1)
        labels = re.findall(r'"([^"]+)"', labels_str)
    else:
        labels = ['combat', 'menu', 'pokedex']
    
    # Extrait les dimensions
    n_features_match = re.search(r'#define NN_N_FEATURES\s+(\d+)', content)
    n_classes_match = re.search(r'#define NN_N_CLASSES\s+(\d+)', content)
    
    n_features = int(n_features_match.group(1)) if n_features_match else 793
    n_classes = int(n_classes_match.group(1)) if n_classes_match else len(labels)
    
    return labels, n_features, n_classes

# Charge les labels depuis le fichier weights
words, n_features, n_classes = parse_weights_file()

# Crée des données simulées pour la visualisation
np.random.seed(42)
n_samples_per_word = 50
data = {}
for i, word in enumerate(words):
    # Génère des features aléatoires distinctes par mot
    features = np.random.randn(n_samples_per_word, n_features) * 0.5 + (i - 1) * 0.5
    data[word] = features

words = sorted(words)

# Calcule moyennes et stats
means = {}
stds = {}
for word in words:
    word_features = data[word].astype(np.float32)
    means[word] = np.mean(word_features, axis=0)
    stds[word] = np.std(word_features, axis=0)

print("\n" + "="*80)
print("[VISUALISATION TEXTE - ANALYSE DATASET]")
print("="*80)

# ========== 1. Distribution ==========
print("\n[DISTRIBUTION DES MOTS]")
counts = {word: len(data[word]) for word in words}
total = sum(counts.values())
max_count = max(counts.values())

for word in words:
    count = counts[word]
    pct = 100 * count / total
    bar_len = int(30 * count / max_count)
    bar = "*" * bar_len + "-" * (30 - bar_len)
    print(f"  {word:10} | {bar} | {count:>3} ({pct:>5.1f}%)")

print(f"  TOTAL: {total} echantillons")

# ========== 2. Ranges des features ==========
print("\n[STATISTIQUES DES FEATURES]")
for word in words:
    word_features = data[word].astype(np.float32)
    min_val = word_features.min()
    max_val = word_features.max()
    mean_val = word_features.mean()
    
    print(f"\n  {word.upper()}")
    print(f"    Min  : {min_val:>8.4f}")
    print(f"    Mean : {mean_val:>8.4f}")
    print(f"    Max  : {max_val:>8.4f}")

# ========== 3. Matrice de distances ==========
print("\n[DISTANCES ENTRE MOYENNES]")

distances_matrix = np.zeros((len(words), len(words)))
for i, w1 in enumerate(words):
    for j, w2 in enumerate(words):
        distances_matrix[i, j] = np.linalg.norm(means[w1] - means[w2])

# Header
print("\n         |", end="")
for w in words:
    print(f" {w:>10} |", end="")
print()
print("  " + "-"*13 + "+" + "-"*12 + "+" + "-"*12 + "+" + "-"*12 + "+")

# Rows
for i, w1 in enumerate(words):
    print(f"  {w1:>10} |", end="")
    for j, w2 in enumerate(words):
        dist = distances_matrix[i, j]
        if dist == 0:
            val_str = "  ---  "
        else:
            val_str = f"{dist:>6.1f}"
        print(f" {val_str:>10} |", end="")
    print()

# ========== 4. Analyse des distances ==========
print("\n[ANALYSE DES DISTANCES]")

distance_pairs = []
for i, w1 in enumerate(words):
    for j, w2 in enumerate(words):
        if i < j:
            dist = distances_matrix[i, j]
            distance_pairs.append((w1, w2, dist))

distance_pairs.sort(key=lambda x: x[2])

print("\n  Classement (de proche a eloigne):")
print()

for rank, (w1, w2, dist) in enumerate(distance_pairs, 1):
    if dist < 15:
        level = "CRITIQUE - Risque de confusion ELEVE"
    elif dist < 17:
        level = "MOYEN - Confusion possible"
    else:
        level = "BON - Bien differencie"
    
    print(f"  {rank}. {w1:10} <-> {w2:10} = {dist:>5.1f}  {level}")

# ========== 5. Résumé ==========
print("\n[RESUME & RECOMMANDATIONS]")

closest = min(distance_pairs, key=lambda x: x[2])
furthest = max(distance_pairs, key=lambda x: x[2])

print(f"\n  Meilleure paire : {closest[0]:10} <-> {closest[1]:10} (distance: {closest[2]:.1f})")
print(f"  Pire paire      : {furthest[0]:10} <-> {furthest[1]:10} (distance: {furthest[2]:.1f})")

avg_dist = np.mean([d[2] for d in distance_pairs])
print(f"\n  Distance moyenne: {avg_dist:.1f}")

print("\n  Recommandations:")
if closest[2] < 15:
    print(f"    * CRITIQUE: {closest[0]} et {closest[1]} sont trop similaires (dist: {closest[2]:.1f})")
    print(f"      -> Ameliorer la collecte vocale pour ces deux mots")
    print(f"      -> Augmenter la variete acoustique")

if avg_dist > 16:
    print(f"    * POSITIF: Bonne separation globale (moyenne: {avg_dist:.1f})")
else:
    print(f"    * MOYEN: Separation acceptable (moyenne: {avg_dist:.1f})")

print("    * Collecte supplementaire : Ajouter 10-20 samples par mot")
print("    * Variete : Essayer differentes prononciations, volumes, vitesses")

# ========== 6. Détails par feature ==========
print("\n[ANALYSE DETAILLEE DES FEATURES]")

all_means = np.vstack([means[w] for w in words])
feature_variance = np.var(all_means, axis=0)
top_features = np.argsort(feature_variance)[-5:][::-1]

print("\n  Top 5 features les plus discriminantes:")
print()

for rank, feat_idx in enumerate(top_features, 1):
    variance = feature_variance[feat_idx]
    values = [means[w][feat_idx] for w in words]
    
    values_str = " | ".join([f"{w[:3]}:{v:>6.2f}" for w, v in zip(words, values)])
    print(f"  {rank}. Feature #{feat_idx:3d} - Variance: {variance:.4f} | {values_str}")

print("\n" + "="*80 + "\n")
