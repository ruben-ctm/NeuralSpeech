#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NeuralSpeech — Moniteur OLED pixel-perfect
Reçoit [FB_ROUGE] et [FB_BLEU] depuis l'Arduino et reconstruit
exactement ce que les deux OLEDs affichent, pixel par pixel.
"""

import serial
import serial.tools.list_ports
import threading
import time
import re
from collections import deque
import tkinter as tk
from tkinter import messagebox, scrolledtext
import tkinter.font as tkFont

# ── Paramètres écran ──────────────────────────────────────────────────────────
OLED_W   = 128
OLED_H   = 64
SCALE    = 5                        # 128×64 → 640×320

CANVAS_W = OLED_W * SCALE
CANVAS_H = OLED_H * SCALE

# Couleurs
BG          = '#0a0a0f'
PIXEL_ON    = '#00e5ff'             # Cyan — couleur des pixels OLED allumés
PIXEL_OFF   = '#030308'             # Quasi-noir — pixels éteints (légère teinte)
BORDER_R    = '#cc2222'
BORDER_B    = '#2255cc'
LABEL_R     = '#ff4444'
LABEL_B     = '#4488ff'
LOG_BG      = '#080810'
LOG_FG      = '#00cc66'
VOCAL_COLOR = '#00e5ff'


# ── Thread série ──────────────────────────────────────────────────────────────
class FullMonitor:
    FB_ROUGE_PFX = '[FB_ROUGE]'
    FB_BLEU_PFX  = '[FB_BLEU]'
    VOCAL_PAT    = re.compile(r'Vocal:\s*(\w+)\s*\((\d+\.?\d*)%\)', re.IGNORECASE)

    def __init__(self, port, baudrate=115200):
        self.ser        = None
        self.running    = False
        self.port       = port
        self.baudrate   = baudrate
        self.lock       = threading.Lock()
        self.rouge_fb   = None
        self.bleu_fb    = None
        self.last_vocal = ''
        self.vocal_ts   = 0.0
        self.serial_log = deque(maxlen=300)

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            return True
        except Exception as e:
            print(f'Erreur connexion: {e}')
            return False

    def _parse_fb(self, line, prefix):
        hex_str = line[len(prefix):]
        if len(hex_str) == 2048:
            try:
                return bytes.fromhex(hex_str)
            except ValueError:
                pass
        return None

    def read_serial(self):
        while self.running:
            if self.ser and self.ser.in_waiting:
                try:
                    raw  = self.ser.readline()
                    line = raw.decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue
                    with self.lock:
                        if line.startswith(self.FB_ROUGE_PFX):
                            fb = self._parse_fb(line, self.FB_ROUGE_PFX)
                            if fb:
                                self.rouge_fb = fb
                        elif line.startswith(self.FB_BLEU_PFX):
                            fb = self._parse_fb(line, self.FB_BLEU_PFX)
                            if fb:
                                self.bleu_fb = fb
                        else:
                            self.serial_log.append(line)
                            m = self.VOCAL_PAT.search(line)
                            if m:
                                self.last_vocal = f"{m.group(1).upper()}  {m.group(2)} %"
                                self.vocal_ts   = time.time()
                except Exception as e:
                    print(f'Erreur lecture: {e}')
            time.sleep(0.002)

    def start(self):
        self.running = True
        threading.Thread(target=self.read_serial, daemon=True).start()

    def stop(self):
        self.running = False
        if self.ser:
            self.ser.close()

    def snapshot(self):
        with self.lock:
            vocal = self.last_vocal if time.time() - self.vocal_ts < 4 else ''
            return self.rouge_fb, self.bleu_fb, vocal, list(self.serial_log)


# ── Interface graphique ───────────────────────────────────────────────────────
class MonitorGUI:
    def __init__(self, root):
        self.root       = root
        self.root.title('NeuralSpeech — Moniteur OLED')
        self.root.configure(bg=BG)
        self.root.resizable(True, True)

        self.monitor    = None
        self.fullscreen = False

        # Images pixel-par-pixel (PhotoImage tkinter)
        self.img_rouge = tk.PhotoImage(width=CANVAS_W, height=CANVAS_H)
        self.img_bleu  = tk.PhotoImage(width=CANVAS_W, height=CANVAS_H)

        # Cache pour éviter de re-rendre si rien n'a changé
        self._prev_rouge = None
        self._prev_bleu  = None
        self._log_shown  = 0

        # Pré-calculer la palette (1024 octets SSD1306 → pixels)
        # Chaque pixel est soit PIXEL_ON soit PIXEL_OFF, répété SCALE fois
        self._row_on  = PIXEL_ON
        self._row_off = PIXEL_OFF

        self._build_ui()
        self._blank(self.img_rouge)
        self._blank(self.img_bleu)
        self._refresh()

    # ── UI ────────────────────────────────────────────────────────────────────
    def _build_ui(self):
        font_title  = tkFont.Font(family='Courier New', size=13, weight='bold')
        font_vocal  = tkFont.Font(family='Courier New', size=22, weight='bold')
        font_status = tkFont.Font(family='Courier New', size=10)
        font_log    = tkFont.Font(family='Courier New', size=9)

        # ── Barre de contrôle ─────────────────────────────────────────────────
        bar = tk.Frame(self.root, bg='#111118', pady=8)
        bar.pack(side=tk.TOP, fill=tk.X)

        tk.Label(bar, text='PORT:', bg='#111118', fg='#555577',
                 font=font_log).pack(side=tk.LEFT, padx=(14, 4))

        self.port_var = tk.StringVar(value='COM5')
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if ports and 'COM5' not in ports:
            self.port_var.set(ports[0])
        opt = tk.OptionMenu(bar, self.port_var, *(ports if ports else ['COM5']))
        opt.config(bg='#1a1a2a', fg=PIXEL_ON, activebackground='#2a2a3a',
                   activeforeground=PIXEL_ON, highlightthickness=0,
                   font=font_log, relief='flat')
        opt['menu'].config(bg='#1a1a2a', fg=PIXEL_ON, font=font_log)
        opt.pack(side=tk.LEFT, padx=4)

        self._btn(bar, 'CONNECTER',    self.connect,    '#0d3d1a').pack(side=tk.LEFT, padx=4)
        self._btn(bar, 'DÉCONNECTER', self.disconnect, '#3d0d0d').pack(side=tk.LEFT, padx=4)
        self._btn(bar, 'PLEIN ÉCRAN', self.toggle_fs,  '#0d1a3d').pack(side=tk.LEFT, padx=10)

        self.status_lbl = tk.Label(bar, text='◉ DÉCONNECTÉ', bg='#111118',
                                   fg='#663333', font=font_status)
        self.status_lbl.pack(side=tk.LEFT, padx=14)

        # ── Deux écrans OLED — centrés quoi qu'il arrive ─────────────────────
        screens_outer = tk.Frame(self.root, bg=BG)
        screens_outer.pack(fill=tk.X, expand=False, pady=10)

        screens = tk.Frame(screens_outer, bg=BG)
        screens.pack(anchor='center')

        self.canvas_rouge = self._make_screen(screens, 'ÉCRAN ROUGE', BORDER_R,
                                              LABEL_R, self.img_rouge, font_title)
        self.canvas_bleu  = self._make_screen(screens, 'ÉCRAN BLEU',  BORDER_B,
                                              LABEL_B, self.img_bleu,  font_title)

        # ── Bandeau vocal ──────────────────────────────────────────────────────
        vocal_frm = tk.Frame(self.root, bg='#0c0c14', height=52)
        vocal_frm.pack(fill=tk.X, padx=16, pady=(0, 6))
        vocal_frm.pack_propagate(False)

        tk.Label(vocal_frm, text='▶ VOCAL', bg='#0c0c14', fg='#334455',
                 font=('Courier New', 9)).pack(side=tk.LEFT, padx=(14, 10))
        self.vocal_lbl = tk.Label(vocal_frm, text='', bg='#0c0c14',
                                  fg=VOCAL_COLOR, font=font_vocal)
        self.vocal_lbl.pack(side=tk.LEFT)

        # ── Log série ──────────────────────────────────────────────────────────
        log_frm = tk.Frame(self.root, bg=BG)
        log_frm.pack(fill=tk.BOTH, expand=True, padx=16, pady=(0, 10))

        tk.Label(log_frm, text='LOG SÉRIE', bg=BG, fg='#334455',
                 font=('Courier New', 8)).pack(anchor='w')

        self.log_text = scrolledtext.ScrolledText(
            log_frm, height=6, font=font_log,
            bg=LOG_BG, fg=LOG_FG, insertbackground=LOG_FG,
            relief='flat', borderwidth=0, selectbackground='#1a3a1a')
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.root.bind('<F11>',    lambda e: self.toggle_fs())
        self.root.bind('<Escape>', lambda e: self._exit_fs())
        self.root.update_idletasks()
        # Taille minimale
        self.root.minsize(CANVAS_W * 2 + 120, CANVAS_H + 300)

    def _btn(self, parent, text, cmd, bg):
        return tk.Button(parent, text=text, command=cmd,
                         bg=bg, fg=PIXEL_ON, relief='flat',
                         font=('Courier New', 9, 'bold'),
                         padx=10, pady=4, cursor='hand2',
                         activebackground=bg, activeforeground='white',
                         borderwidth=0)

    def _make_screen(self, parent, label, border_color, label_color, img, font):
        frm = tk.Frame(parent, bg=BG)
        frm.pack(side=tk.LEFT, padx=12, pady=4)

        tk.Label(frm, text=label, bg=BG, fg=label_color,
                 font=font).pack(pady=(0, 6))

        # Bordure colorée autour de l'écran
        border = tk.Frame(frm, bg=border_color, padx=2, pady=2)
        border.pack()

        # Fond OLED (légèrement teinté, pas pure black)
        inner = tk.Frame(border, bg='#010105', padx=0, pady=0)
        inner.pack()

        canvas = tk.Canvas(inner, width=CANVAS_W, height=CANVAS_H,
                           bg='#010105', highlightthickness=0)
        canvas.pack()
        canvas.create_image(0, 0, image=img, anchor='nw')
        return canvas

    # ── Connexion ─────────────────────────────────────────────────────────────
    def connect(self):
        try:
            self.monitor = FullMonitor(self.port_var.get())
            if self.monitor.connect():
                self.monitor.start()
                self.status_lbl.config(text='◉ CONNECTÉ', fg='#00cc55')
            else:
                messagebox.showerror('Erreur', 'Impossible de se connecter au port')
                self.monitor = None
        except Exception as e:
            messagebox.showerror('Erreur', str(e))

    def disconnect(self):
        if self.monitor:
            self.monitor.stop()
            self.monitor = None
        self.status_lbl.config(text='◉ DÉCONNECTÉ', fg='#663333')
        self._blank(self.img_rouge)
        self._blank(self.img_bleu)
        self._prev_rouge = None
        self._prev_bleu  = None
        self._log_shown  = 0
        self.log_text.delete('1.0', tk.END)

    def toggle_fs(self):
        self.fullscreen = not self.fullscreen
        self.root.attributes('-fullscreen', self.fullscreen)

    def _exit_fs(self):
        self.fullscreen = False
        self.root.attributes('-fullscreen', False)

    # ── Rendu pixel-par-pixel ─────────────────────────────────────────────────
    def _blank(self, img):
        """Écran noir au démarrage."""
        row = tuple([PIXEL_OFF] * CANVAS_W)
        img.put([row] * CANVAS_H)

    def _render_fb(self, img, fb):
        """
        Décode le framebuffer SSD1306 (1024 octets) → image Tkinter.

        Format SSD1306 :
          - 8 pages × 128 colonnes
          - Dans chaque octet : bit 0 = pixel en HAUT de la page (y faible)
          - page p, colonne c, bit b → pixel (x=c, y=p*8+b)
        """
        # Construire une grille [y][x] de booléens
        pixels = [[False] * OLED_W for _ in range(OLED_H)]
        for page in range(8):
            for col in range(OLED_W):
                byte = fb[page * OLED_W + col]
                for bit in range(8):
                    if (byte >> bit) & 1:
                        pixels[page * 8 + bit][col] = True

        # Construire les lignes tkinter (SCALE × zoom)
        rows = []
        for y in range(OLED_H):
            row_pixels = []
            for x in range(OLED_W):
                c = PIXEL_ON if pixels[y][x] else PIXEL_OFF
                row_pixels.extend([c] * SCALE)
            row_t = tuple(row_pixels)
            for _ in range(SCALE):
                rows.append(row_t)

        img.put(rows)

    # ── Boucle de rafraîchissement ─────────────────────────────────────────────
    def _refresh(self):
        if self.monitor:
            rouge_fb, bleu_fb, vocal, logs = self.monitor.snapshot()

            # Mettre à jour l'écran rouge si changé
            if rouge_fb is not None and rouge_fb != self._prev_rouge:
                self._render_fb(self.img_rouge, rouge_fb)
                self._prev_rouge = rouge_fb

            # Mettre à jour l'écran bleu si changé
            if bleu_fb is not None and bleu_fb != self._prev_bleu:
                self._render_fb(self.img_bleu, bleu_fb)
                self._prev_bleu = bleu_fb

            # Bandeau vocal
            self.vocal_lbl.config(text=vocal)

            # Statut
            if rouge_fb is not None:
                self.status_lbl.config(text='◉ CONNECTÉ — LIVE', fg='#00cc55')
            elif logs and rouge_fb is None:
                self.status_lbl.config(
                    text='⚠ Re-téléverser le sketch (FB désactivé)',
                    fg='#ffaa00')

            # Log série incrémental
            if len(logs) > self._log_shown:
                new_lines = logs[self._log_shown:]
                at_bottom = self.log_text.yview()[1] >= 0.98
                for line in new_lines:
                    self.log_text.insert(tk.END, line + '\n')
                self._log_shown = len(logs)
                if at_bottom:
                    self.log_text.see(tk.END)

        self.root.after(40, self._refresh)   # ~25 fps


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    root = tk.Tk()
    app  = MonitorGUI(root)
    root.mainloop()


if __name__ == '__main__':
    main()
