\# ⏱️ Pomodoro Timer ESP32-C3 Super Mini



\[!\[Platform: ESP32](https://img.shields.io/badge/Platform-ESP32--C3-blue?style=flat-square\&logo=espressif)](https://www.espressif.com/)

\[!\[License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)](https://opensource.org/licenses/MIT)



Un minuteur Pomodoro compact et élégant basé sur l'\*\*ESP32-C3 Super Mini\*\*. Ce projet combine un suivi de productivité précis avec une interface visuelle riche (OLED + NeoPixel) et une configuration simplifiée via un portail Web.



\---



\## ✨ Fonctionnalités



\* \*\*Cycles Pomodoro Intelligents\*\* : Gestion automatique des phases de travail et des pauses.

\* \*\*Interface OLED (SSD1306)\*\* : Affichage du temps restant, du mode actuel et des menus de réglages.

\* \*\*Retour Visuel NeoPixel (12 LEDs)\*\* :

&#x20;   \* \*\*Effet Pulse\*\* : Les LEDs "respirent" au rythme des secondes.

&#x20;   \* \*\*Progression\*\* : Le nombre de LEDs allumées diminue avec le temps restant.

&#x20;   \* \*\*Animations de fin\*\* : 5 effets au choix (Arc-en-ciel, Flash, Comète, Respiration, Aucun).

\* \*\*Configuration Hybride\*\* :

&#x20;   \* \*\*Menu Embarqué\*\* : Naviguez avec l'encodeur directement sur l'écran.

&#x20;   \* \*\*Web Portal\*\* : Serveur WiFi intégré pour configurer les couleurs et réglages depuis un smartphone.

\* \*\*Sauvegarde NVS\*\* : Tous vos réglages (couleurs, luminosité, effets) sont conservés même après extinction.

\* \*\*Easter Egg\*\* : Un mode "Crédits" avec décor étoilé animé (accessible via 4 clics rapides).



\---



\## 🛠️ Matériel \& Pinout (ESP32-C3)



| Composant | Pin ESP32-C3 | Description |

| :--- | :--- | :--- |

| \*\*OLED SDA\*\* | GPIO 8 | Données I2C |

| \*\*OLED SCL\*\* | GPIO 9 | Horloge I2C |

| \*\*NeoPixel\*\* | GPIO 2 | Signal Data (DIN) |

| \*\*Encodeur A\*\* | GPIO 4 | Phase A |

| \*\*Encodeur B\*\* | GPIO 5 | Phase B |

| \*\*Bouton\*\* | GPIO 3 | Switch de l'encodeur |



\---



\## 📂 Utilisation des Menus



\### Commandes de base

\* \*\*Tourner l'encodeur\*\* : Ajuste le temps (en mode SET) ou navigue dans les menus.

\* \*\*Appui court\*\* : Lance le timer ou valide une sélection.

\* \*\*Appui long (800ms)\*\* : Ouvre ou ferme le menu de configuration.

\* \*\*4 clics rapides\*\* : Déclenche l'Easter Egg (Crédits).



\### Structure du Menu OLED

1\.  \*\*Couleur Work\*\* : Définit la couleur du cycle de travail.

2\.  \*\*Couleur Pause\*\* : Définit la couleur du cycle de repos.

3\.  \*\*Couleur Config\*\* : Couleur des LEDs en mode réglage.

4\.  \*\*Effet fin\*\* : Animation lancée quand le timer arrive à zéro.

5\.  \*\*Luminosité\*\* : Ajustement de 0 à 100%.

6\.  \*\*Web Portal\*\* : Affiche les identifiants WiFi pour la configuration distante.



\---



\## 🌐 Portail Web Captif



Pour une configuration avancée, activez le mode Web Portal.

\* \*\*SSID\*\* : `POMODOROX`

\* \*\*Mot de passe\*\* : \*(Aucun)\*

\* \*\*Accès\*\* : Connectez-vous au WiFi et allez sur `http://192.168.4.1`



L'interface vous permet de choisir précisément vos couleurs via un sélecteur hexadécimal et de tester les effets en temps réel.



\---



\## 🚀 Installation



1\.  Assurez-vous d'avoir installé les bibliothèques suivantes dans l'IDE Arduino :

&#x20;   \* `Adafruit SSD1306` \& `Adafruit GFX`

&#x20;   \* `Adafruit NeoPixel`

&#x20;   \* `Encoder` (par Paul Stoffregen)

2\.  Ajoutez le fichier `boot\_bitmap.h` dans le même dossier que votre sketch pour l'image de démarrage.

3\.  Sélectionnez la carte \*\*ESP32-C3 Dev Module\*\* et téléversez.



\---



\## 👨‍💻 Crédits



\* \*\*Code original\*\* : WILLAU

\* \*\*Mise à jour \& Design\*\* : \_n3o\_

