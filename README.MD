# 🍅 POMODOROX Timer — v3.0

> Pomodoro timer physique basé sur un **ESP32-C3 Super Mini**, avec ruban LED NeoPixel, écran OLED et interface web de configuration.

---

## 🛠️ Matériel requis

| Composant | Détail |
|---|---|
| Microcontrôleur | ESP32-C3 Super Mini |
| Écran | OLED SSD1306 128×64 I2C |
| LEDs | Ruban NeoPixel 12 LEDs |
| Encodeur | EC11 rotatif avec bouton |
| Bouton | Switch externe |

### Câblage

| Pin | Fonction |
|---|---|
| GPIO 2 | NeoPixel data |
| GPIO 3 | Bouton |
| GPIO 4 | Encodeur A |
| GPIO 5 | Encodeur B |
| GPIO 8 | SDA (I2C OLED) |
| GPIO 9 | SCL (I2C OLED) |

---

## 📦 Bibliothèques Arduino requises

- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `Adafruit NeoPixel`
- `Encoder`
- `Preferences` *(incluse ESP32)*
- `WiFi` + `WebServer` *(incluses ESP32)*

---

## 🚀 Boot Menu

Au démarrage, après le boot screen, un **menu de sélection de mode** s'affiche avec icône centrale animée et points de navigation.

| Mode | Icône | Description |
|---|---|---|
| POMODORO | 🍅 Tomate | Timer Pomodoro classique |
| TIMER | ⏳ Sablier | Minuteur simple configurable |
| CHRONO | ⏱️ Chronomètre | Chronomètre start/stop/reset |
| AMBIANCE | 💡 Ampoule | Effets LED d'ambiance |

Navigation : **encodeur** pour défiler, **bouton court** pour sélectionner. Depuis n'importe quel mode, un **appui long** permet de revenir au boot menu.

---

## ✅ Modes disponibles

### ⏱️ Mode POMODORO

- Timer configurable par pas de 30 min (30 min → 12h)
- Ruban LED indiquant le temps restant (LEDs qui s'éteignent progressivement)
- Pauses automatiques toutes les 25 min de travail (5 min de pause)
- Affichage OLED du temps restant centré en grand
- Encodeur pour régler la durée en MODE_SET
- Bouton court : start/stop — Bouton long : menu config

### ⏳ Mode TIMER SIMPLE

- Minuteur simple de 1 min à 12h
- Encodeur pour régler la durée (tournez avant de démarrer)
- Bouton court : start/pause/reprendre
- LEDs progressives indiquant le temps restant
- Retour automatique au boot menu à la fin

### ⏱️ Mode CHRONO

- Chronomètre classique avec affichage HH:MM:SS
- Bouton court : start / pause / reprendre
- Appui long : reset et retour au boot menu
- Affichage en temps réel sur l'OLED

### 💡 Mode AMBIANCE

- 3 effets LED en boucle, sélectionnables à l'encodeur
- **Respiration** — fade doux et régulier
- **Arc-en-ciel** — cycle de couleurs continu
- **Comète** — pixel lumineux qui parcourt le ruban
- Appui long pour quitter vers le boot menu

---

## ✨ Fonctionnalités transversales

### 🌈 LEDs & Effets

- **Pulse LED à chaque seconde** — courbe sinus configurable (intensité min/max)
- **4 effets de fin de timer** : Arc-en-ciel, Flash, Comète, Respiration
- **Preview des effets en temps réel** lors de la sélection dans le menu
- **Couleur configurable** pour chaque mode : WORK, PAUSE, CONFIG
- **Flash vert de validation** + coche ✓ sur l'OLED à chaque confirmation dans le menu

### ⚙️ Menu OLED de configuration

Accès par **appui long** en mode Pomodoro. Navigation à l'encodeur, validation par appui court.

| Item | Détail |
|---|---|
| Couleur Work | 9 couleurs prédéfinies, preview live |
| Couleur Pause | idem |
| Couleur Config | idem |
| Effet fin | 5 effets, preview live |
| Luminosité | Slider 10–100%, preview live |
| Police | 12 polices au choix, preview "12:34" live |
| Web portal | Affiche SSID / IP de connexion |
| Retour | Sauvegarde et quitte |

### 🌐 Web Portal

Connexion WiFi : **POMODOROX** (réseau ouvert) → `192.168.4.1`

- Color pickers pour les 3 couleurs de mode
- Sélecteur d'effet de fin de timer
- Slider de luminosité LED
- Sliders pulse (intensité min / max)
- Sélecteur de police avec **preview canvas OLED** en temps réel
- Sauvegarde persistante en **NVS** (survit aux redémarrages)

### 🔤 Polices d'affichage

12 polices disponibles, tailles calibrées pour remplir l'écran :

| # | Nom | Style |
|---|---|---|
| 0 | Builtin | Police par défaut (×4) |
| 1 | FreeSans 9pt | Sans-serif fin |
| 2 | FreeSansBold 9pt | Sans-serif bold |
| 3 | FreeSansBold 12pt | Sans-serif bold large |
| 4 | FreeSansBoldOblique 12pt | Sans-serif bold italique |
| 5 | FreeMono 9pt | Monospace |
| 6 | FreeSerif 9pt | Serif classique |
| 7 | FreeSerifBold 12pt | Serif bold |
| 8 | FreeSerifBoldItalic 9pt | Serif bold italique |
| 9 | Org_01 | Pixel art / LCD rétro (×4) |
| 10 | TomThumb | Micro pixel (×5) |
| 11 | Picopixel | Ultra micro (×5) |

---

## 🥚 Easter Eggs

#### 🎬 Easter Egg 1 — Crédits (4 clics rapides)
- Écran de crédits avec **étoiles filantes** en arrière-plan
- Texte scrollant : *Code original WILLAU / Update & new features _n3o_*
- Arc-en-ciel sur le ruban
- Durée 8s ou appui pour quitter

#### 🕹️ Easter Egg 2 — Arcade (6 clics rapides)
Menu arcade avec 3 jeux, navigation à l'encodeur. Les **high scores sont sauvegardés en NVS** et persistent après redémarrage. L'écran Game Over affiche score + record, avec **flash doré** sur les LEDs en cas de nouveau record.

**🦕 Dino Runner**
- Runner infini style Chrome offline
- Bouton = saut
- Vitesse progressive (1.8 → 4.5 px/frame)
- 2 cactus en rotation, collision précise
- LEDs : barre de score vert → rouge
- High score sauvegardé

**🏓 Pong vs IA**
- Encodeur = raquette joueur (côté droit)
- IA progressive qui s'adapte au score
- Victoire à 7 points
- Countdown 3-2-1-GO discret avec fade LED avant chaque lancer
- LEDs : score joueur (bleu) / score IA (rouge)
- High score sauvegardé

**🧱 Casse-briques**
- 8×4 briques, 3 vies
- Encodeur = raquette, bouton = lancer la balle
- Accélération progressive
- Countdown 3-2-1-GO discret avec fade LED avant chaque lancer
- LEDs : briques restantes (vert → orange)
- High score sauvegardé

---

## 📁 Fichiers du projet

```
POMODOROX/
├── POMODOROX.ino      ← firmware principal
├── boot_bitmap.h      ← logo boot screen (bitmap 61×31px)
└── README.md
```

---

## 🎮 Contrôles résumés

| Action | Effet |
|---|---|
| Encodeur (boot menu) | Naviguer entre les modes |
| Bouton court (boot menu) | Sélectionner un mode |
| Encodeur (MODE_SET) | Régler la durée (par pas de 30 min) |
| Bouton court (MODE_SET) | Démarrer le timer |
| Bouton court (en cours) | Arrêter / reset |
| Bouton long | Retour au boot menu / menu config |
| Encodeur (Timer simple, arrêté) | Régler la durée |
| Bouton court (Timer simple) | Start / pause |
| Bouton court (Chrono) | Start / pause |
| Bouton long (Chrono) | Reset + retour boot menu |
| Encodeur (Ambiance) | Changer l'effet |
| Appui long (Ambiance) | Retour boot menu |
| 4 clics rapides | Easter egg Crédits |
| 6 clics rapides | Easter egg Arcade |
| Bouton long (en jeu) | Retour menu arcade |
| Bouton long (menu arcade) | Retour boot menu |

---

## 👾 Crédits

**Code original** — WILLAU  
**Update & new features** — _n3o_

---

*Built with ❤️ on ESP32-C3*
