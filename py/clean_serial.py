#!/usr/bin/env python3
"""
clean_serial.py - Nettoie l'output du Serial Monitor → dataset.csv
====================================================================
Mots valides : menu, combat, pokedex

Utilisation :
  1. Copier tout le contenu du Serial Monitor dans raw_output.txt
  2. python clean_serial.py
"""

import os, sys

INPUT_FILE   = "raw_output.txt"
OUTPUT_FILE  = "dataset.csv"
N_FRAMES     = 61
N_MFCC       = 13
N_FEATURES   = N_FRAMES * N_MFCC   # 793
VALID_LABELS = {"menu", "combat", "pokedex"}

def main():
    if not os.path.isfile(INPUT_FILE):
        print(f"ERREUR : '{INPUT_FILE}' introuvable.")
        print("  → Copier le contenu du Serial Monitor dans raw_output.txt")
        sys.exit(1)

    data_lines = []
    skipped = errors = 0

    with open(INPUT_FILE, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#') or line.startswith('='):
                skipped += 1
                continue

            parts = line.split(',')
            label = parts[0].strip()

            if not label or label[0].isdigit() or label[0] == '-':
                skipped += 1
                continue

            if label not in VALID_LABELS:
                skipped += 1
                continue

            if len(parts) != 1 + N_FEATURES:
                print(f"[WARN] '{label}' : {len(parts)} valeurs (attendu {1+N_FEATURES})")
                errors += 1
                continue

            data_lines.append(line)

    if not data_lines:
        print("Aucune ligne valide trouvée !")
        sys.exit(1)

    counts = {}
    for line in data_lines:
        lbl = line.split(',')[0]
        counts[lbl] = counts.get(lbl, 0) + 1

    print(f"Lignes valides : {len(data_lines)}")
    print(f"Ignorées       : {skipped} | Erreurs : {errors}")
    for lbl in sorted(counts):
        print(f"  {lbl:10s} : {counts[lbl]:3d}")

    # Écrire TXT (format sans header)
    txt_file = OUTPUT_FILE.replace('.csv', '.txt')
    with open(txt_file, 'a') as out:
        for line in data_lines:
            out.write(line + '\n')

    print(f"\nSauvegardé dans '{txt_file}'")

    # Résumé total
    total_counts = {}
    if os.path.isfile(txt_file):
        with open(txt_file, 'r') as f:
            for line in f:
                lbl = line.split(',')[0].strip()
                if lbl: total_counts[lbl] = total_counts.get(lbl, 0) + 1

    print(f"\nDataset total ({sum(total_counts.values())} échantillons) :")
    for lbl in sorted(total_counts):
        count = total_counts[lbl]
        status = "✓ OK" if count >= 50 else f"⚠ manque {50-count}"
        print(f"  {lbl:10s} : {count:3d}  [{status}]")

if __name__ == '__main__':
    main()