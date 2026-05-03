# 🍅 POMODOROX Timer

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

## ✅ Fonctionnalités de base

- ⏱️ **Timer Pomodoro** configurable par pas de 30 min (30 min → 12h)
- 🔴 **Ruban LED** indiquant le temps restant (LEDs qui s'éteignent progressivement)
- ☕ **Pauses automatiques** toutes les 25 min de travail (5 min de pause)
- 🖥️ **Affichage OLED** du temps restant centré en grand
- 🎛️ **Encodeur rotatif** pour régler la durée en MODE_SET
- 🔘 **Bouton** — appui court : start/stop, appui long : menu config

---

## ✨ Additions

### 🌈 LEDs & Effets

- **Pulse LED à chaque seconde** — la luminosité descend et remonte en douceur (courbe sinus) pour indiquer le passage du temps, configurable (intensité min/max)
- **4 effets de fin de timer** au choix : Arc-en-ciel, Flash, Comète, Respiration
- **Preview des effets en temps réel** sur le ruban lors de la sélection dans le menu
- **Couleur configurable** pour chaque mode : WORK, PAUSE, CONFIG
- **Couleur CONFIG** appliquée automatiquement sur les LEDs pendant la navigation dans les menus
- **Flash vert de validation** sur le ruban + coche ✓ sur l'OLED à chaque confirmation dans le menu

### ⚙️ Menu OLED de configuration

Accès par **appui long** sur le bouton. Navigation à l'encodeur, validation par appui court.

| Item | Détail |
|---|---|
| Couleur Work | 9 couleurs prédéfinies, preview live sur le ruban |
| Couleur Pause | idem |
| Couleur Config | idem |
| Effet fin | 5 effets, preview live sur le ruban |
| Luminosité | Slider 10–100%, preview live |
| Police | 12 polices au choix, preview "12:34" live sur l'OLED |
| Web portal | Affiche SSID / IP de connexion |
| Retour | Sauvegarde et quitte |

### 🌐 Web Portal

Connexion WiFi : **POMODOROX** (réseau ouvert) → `192.168.4.1`

- Color pickers natifs pour les 3 couleurs de mode
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

### 🚀 Boot Screen

- Affichage du **logo bitmap** au démarrage (image custom `boot_bitmap.h`)
- Sweep arc-en-ciel sur le ruban pendant le boot
- Barre de progression + version
- Flash blanc final avant démarrage

### 🥚 Easter Eggs

#### 🎬 Easter Egg 1 — Crédits (4 clics rapides)
- Écran de crédits avec **étoiles filantes** en arrière-plan
- Texte scrollant : *Code original WILLAU / Update & new features _n3o_*
- Arc-en-ciel sur le ruban
- Durée 8s ou appui pour quitter

#### 🕹️ Easter Egg 2 — Arcade (6 clics rapides)
Menu arcade avec 3 jeux, navigation à l'encodeur.

**🦕 Dino Runner**
- Runner infini style Chrome offline
- Bouton = saut
- Vitesse progressive (1.8 → 4.5 px/frame)
- 2 cactus en rotation, collision précise
- LEDs : barre de score vert → rouge

**🏓 Pong vs IA**
- Encodeur = raquette joueur (côté droit)
- IA progressive qui s'adapte au score
- Victoire à 7 points
- Countdown 3-2-1-GO discret avec fade LED avant chaque lancer
- LEDs : score joueur (bleu) / score IA (rouge)

**🧱 Casse-briques**
- 8×4 briques, 3 vies
- Encodeur = raquette, bouton = lancer la balle
- Accélération progressive
- Countdown 3-2-1-GO discret avec fade LED avant chaque lancer
- LEDs : briques restantes (vert → orange)

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
| Encodeur (MODE_SET) | Régler la durée (par pas de 30 min) |
| Bouton court (MODE_SET) | Démarrer le timer |
| Bouton court (en cours) | Arrêter / reset |
| Bouton long | Ouvrir / fermer le menu config |
| 4 clics rapides | Easter egg Crédits |
| 6 clics rapides | Easter egg Arcade |
| Bouton long (en jeu) | Retour menu arcade |
| Bouton long (menu arcade) | Retour timer |

---

## 👾 Crédits

**Code original** — WILLAU  
**Update & new features** — _n3o_

---

*Built with ❤️ on ESP32-C3*
