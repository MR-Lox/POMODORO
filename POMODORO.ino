/*
 * Pomodoro Timer ESP32-C3 Super Mini
 * - Ecran OLED SSD1306 128x64 I2C
 * - Encodeur EC11
 * - Ruban NeoPixel 12 LEDs
 * - Web portal WiFi AP pour configuration
 * - Menu OLED de configuration
 * - Effets LED : pulse secondes, animation fin de timer
 * - Persistance des réglages en NVS (Preferences)
 *
 * Code original : WILLAU
 * Update        : _n3o_
 *
 * Easter egg : 4 appuis rapides sur le bouton → écran crédits
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <Encoder.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include "boot_bitmap.h"  // bitmap boot screen 61x31

// Polices Adafruit GFX
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include <Fonts/Org_01.h>
#include <Fonts/TomThumb.h>
#include <Fonts/Picopixel.h>

// =====================
// PINS & HARDWARE
// =====================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define SDA_PIN         8
#define SCL_PIN         9
#define LED_PIN         2
#define LED_COUNT      12
#define ENC_A           4
#define ENC_B           5
#define BTN             3

// =====================
// CONSTANTES TIMER
// =====================
#define BREAK_MINUTES           5
#define BREAK_INTERVAL_MINUTES 25
#define STEP_MINUTES           30
#define BREAK_INTERVAL_SECONDS (BREAK_INTERVAL_MINUTES * 60)

// =====================
// WIFI AP
// =====================
#define AP_SSID "POMODOROX"
#define AP_PASS ""

// =====================
// EFFETS FIN DE TIMER
// =====================
#define FX_NONE       0
#define FX_RAINBOW    1
#define FX_FLASH      2
#define FX_COMET      3
#define FX_BREATHE    4

// =====================
// OBJETS HARDWARE
// =====================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Encoder encoder(ENC_A, ENC_B);
Preferences prefs;
WebServer server(80);

// =====================
// CONFIG (persistée)
// =====================
struct Config {
  uint32_t colorWork;
  uint32_t colorBreak;
  uint32_t colorSet;
  uint8_t  fxEnd;         // effet fin de timer
  uint8_t  brightness;    // 0-255
  float    pulseMin;      // facteur min pulse (ex: 0.3)
  float    pulseMax;      // facteur max pulse (ex: 1.0)
  uint8_t  fontIndex;     // index police sélectionnée
};

Config cfg;

// Valeurs par défaut
const Config cfgDefault = {
  Adafruit_NeoPixel::Color(255, 0, 255),   // work : magenta
  Adafruit_NeoPixel::Color(0, 157, 246),   // break : cyan
  Adafruit_NeoPixel::Color(60, 60, 60),    // set   : gris
  FX_RAINBOW,                               // effet fin
  100,                                      // luminosité
  0.3f,                                     // pulseMin
  1.0f,                                     // pulseMax
  0                                         // fontIndex (0 = builtin)
};

// =====================
// MODES PRINCIPAUX
// =====================
enum Mode { MODE_SET, MODE_WORK, MODE_BREAK, MODE_DONE, MODE_CREDITS, MODE_ARCADE, MODE_BOOT_MENU, MODE_TIMER_SIMPLE, MODE_CHRONO, MODE_AMBIANCE };
Mode mode = MODE_SET;

// =====================
// ETAT TIMER
// =====================
int workMinutes              = STEP_MINUTES;
int workRemainingSeconds     = STEP_MINUTES * 60;
int breakRemainingSeconds    = 0;
int remainingSeconds         = STEP_MINUTES * 60;
int workElapsedSinceBreak    = 0;
unsigned long totalWorkSeconds = 0;
unsigned long lastTick       = 0;
unsigned long doneStartMs    = 0; // timestamp entrée en MODE_DONE

// =====================
// EASTER EGG & CREDITS
// =====================
#define EGG_CLICKS      4
#define EGG2_CLICKS     6      // 6 clics → Menu Arcade
#define EGG_WINDOW_MS   2000   // fenêtre de détection (ms)
#define CREDITS_DURATION 8000  // durée affichage crédits (ms)

int   eggClickCount    = 0;
unsigned long eggFirstClick = 0;
unsigned long creditsStartMs = 0;

// =====================
// ARCADE
// =====================
enum GameID { GAME_NONE, GAME_DINO, GAME_PONG, GAME_BREAKOUT };
GameID currentGame = GAME_NONE;
int    arcadeMenuIndex = 0;
#define ARCADE_GAME_COUNT 3

// --- DINO ---
#define DINO_GROUND_Y   54       // y du sol
#define DINO_X          18       // x fixe du dino
#define DINO_W           8
#define DINO_H          12
#define DINO_JUMP_VY   -5.5f    // vitesse initiale saut
#define DINO_GRAVITY    0.45f
#define CACTUS_W        6
#define CACTUS_H_MIN    8
#define CACTUS_H_MAX   16
#define CACTUS_COUNT    2

struct Cactus {
  float x;
  int   h;
  bool  active;
};

struct DinoState {
  float y;
  float vy;
  bool  onGround;
  Cactus cacti[CACTUS_COUNT];
  float speed;
  int   score;
  bool  dead;
  unsigned long lastFrame;
};
DinoState dino;

// --- PONG ---
#define PONG_PADDLE_H   14
#define PONG_PADDLE_W    3
#define PONG_BALL_SIZE   2
#define PONG_WIN_SCORE   7

struct PongState {
  float ballX, ballY;
  float ballVX, ballVY;
  float playerY;   // raquette joueur (droite)
  float aiY;       // raquette IA (gauche)
  int   scorePlayer;
  int   scoreAI;
  bool  paused;    // true = attente lancer
  unsigned long lastFrame;
};
PongState pong;

// --- BREAKOUT ---
#define BRK_COLS        8
#define BRK_ROWS        4
#define BRK_BRICK_W    14
#define BRK_BRICK_H     5
#define BRK_BRICK_GAP   1
#define BRK_PADDLE_W   20
#define BRK_PADDLE_H    3
#define BRK_PADDLE_Y   60
#define BRK_BALL_R      2

struct BreakoutState {
  bool  bricks[BRK_ROWS][BRK_COLS];
  float ballX, ballY;
  float ballVX, ballVY;
  float paddleX;
  int   score;
  int   lives;
  bool  launched;
  unsigned long lastFrame;
};
BreakoutState brk;

// Etoiles pour l'arrière-plan crédits
#define STAR_COUNT 20
struct Star {
  int x, y;
  int speed;  // pixels/frame (1 ou 2)
};
Star stars[STAR_COUNT];
unsigned long lastStarUpdate = 0;

// =====================
// MENU OLED
// =====================
enum MenuState {
  MENU_NONE,        // pas de menu affiché
  MENU_MAIN,        // menu principal
  MENU_COLOR_WORK,  // choix couleur work
  MENU_COLOR_BREAK,
  MENU_COLOR_SET,
  MENU_FX,          // choix effet fin
  MENU_BRIGHT,      // luminosité
  MENU_FONT         // choix police
};

MenuState menuState  = MENU_NONE;
int       menuIndex  = 0;   // sélection courante dans le menu
long      menuEnterEncoder = 0; // position encodeur à l'entrée menu

// Items menu principal
const char* mainMenuItems[] = {
  "Couleur Work",
  "Couleur Pause",
  "Couleur Config",
  "Effet fin",
  "Luminosite",
  "Police",
  "Web portal",
  "Retour"
};
const int MAIN_MENU_COUNT = 8;

// Couleurs prédéfinies pour le menu couleur
const uint32_t presetColors[] = {
  Adafruit_NeoPixel::Color(255,   0, 255), // magenta
  Adafruit_NeoPixel::Color(  0, 157, 246), // cyan
  Adafruit_NeoPixel::Color(255,   0,   0), // rouge
  Adafruit_NeoPixel::Color(  0, 255,   0), // vert
  Adafruit_NeoPixel::Color(  0,   0, 255), // bleu
  Adafruit_NeoPixel::Color(255, 165,   0), // orange
  Adafruit_NeoPixel::Color(255, 255,   0), // jaune
  Adafruit_NeoPixel::Color(255, 255, 255), // blanc
  Adafruit_NeoPixel::Color( 60,  60,  60), // gris
};
const int PRESET_COLOR_COUNT = 9;

const char* presetColorNames[] = {
  "Magenta", "Cyan", "Rouge", "Vert",
  "Bleu", "Orange", "Jaune", "Blanc", "Gris"
};

const char* fxNames[] = {
  "Aucun", "Arc-en-ciel", "Flash", "Comete", "Respiration"
};
const int FX_COUNT = 5;

// =====================
// POLICES
// =====================
#define FONT_COUNT 12

const char* fontNames[FONT_COUNT] = {
  "Builtin",           // 0
  "FreeSans 9",        // 1
  "FreeSansBold 9",    // 2
  "FreeSansBold 12",   // 3
  "FreeSansBoldObl 12",// 4
  "FreeMono 9",        // 5
  "FreeSerif 9",       // 6
  "FreeSerifBold 12",  // 7
  "FreeSerifBoldIt 9", // 8
  "Org_01",            // 9
  "TomThumb",          // 10
  "Picopixel"          // 11
};

// Taille setTextSize à utiliser avec chaque police
// (les GFX custom ignorent setTextSize, mais on garde 1 pour cohérence)
// Taille fixe par police — calibrée pour que chaque police remplisse
// l'écran avec les mêmes proportions que la Builtin x4
// Objectif : "00:00" dans ~100px large x 35px haut
const uint8_t fontTextSize[FONT_COUNT] = {
  4,  //  0  Builtin           → référence
  2,  //  1  FreeSans 9pt      → 47*2=94px large, 26px haut
  2,  //  2  FreeSansBold 9pt  → 49*2=98px large, 26px haut
  2,  //  3  FreeSansBold 12pt → 62*2=124px large, 34px haut
  2,  //  4  FreeSansBoldObl12 → idem
  2,  //  5  FreeMono 9pt      → 50*2=100px large, 26px haut
  2,  //  6  FreeSerif 9pt     → 45*2=90px large, 28px haut
  2,  //  7  FreeSerifBold 12pt→ 60*2=120px large, 36px haut
  2,  //  8  FreeSerifBoldIt 9 → 47*2=94px large, 28px haut
  4,  //  9  Org_01            → 30*4=120px large, 24px haut (pixel art net)
  5,  // 10  TomThumb          → 21*5=105px large, 25px haut
  5   // 11  Picopixel         → 19*5=95px large, 20px haut
};

// Retourne le pointeur GFXfont (nullptr = police builtin)
const GFXfont* getFont(uint8_t idx) {
  switch(idx) {
    case  1: return &FreeSans9pt7b;
    case  2: return &FreeSansBold9pt7b;
    case  3: return &FreeSansBold12pt7b;
    case  4: return &FreeSansBoldOblique12pt7b;
    case  5: return &FreeMono9pt7b;
    case  6: return &FreeSerif9pt7b;
    case  7: return &FreeSerifBold12pt7b;
    case  8: return &FreeSerifBoldItalic9pt7b;
    case  9: return &Org_01;
    case 10: return &TomThumb;
    case 11: return &Picopixel;
    default: return nullptr; // builtin
  }
}

// =====================
// ENCODEUR / BOUTON
// =====================
long lastEncoderPos  = 0;
long encPosPong      = 0;  // position encodeur dédiée Pong
long encPosBreakout  = 0;  // position encodeur dédiée Breakout
long encPosBootMenu  = 0;  // position encodeur dédiée Boot Menu
bool lastButtonState = HIGH;
unsigned long btnPressTime = 0;
bool btnLongHandled = false;

// =====================
// BOOT MENU
// =====================
#define BOOT_MENU_COUNT 4
int bootMenuIndex = 0;

// =====================
// HIGH SCORES ARCADE
// =====================
int hsDino     = 82;
int hsPong     = 0;
int hsBreakout = 0;

// =====================
// TIMER SIMPLE
// =====================
int           timerSimpleMinutes   = 5;
int           timerSimpleRemaining = 5 * 60;
bool          timerSimpleRunning   = false;
unsigned long timerSimpleLast      = 0;

// =====================
// CHRONOMETRE
// =====================
unsigned long chronoElapsed = 0;
bool          chronoRunning = false;
unsigned long chronoStart   = 0;

// =====================
// AMBIANCE
// =====================
int ambianceEffect = 0;

// =====================
// PROTOTYPES
// =====================
void loadConfig();
void saveConfig();
void handleEncoder();
void handleButton();
void updateTimer();
void drawScreen();
void updateLEDs();
void drawMenu();
void enterMenu(MenuState s);
void exitMenu();
void menuNavigate(int delta);
void menuSelect();
void previewColorOnStrip(uint32_t color);
void previewFxOnStrip(uint8_t fx, unsigned long t);
String formatDuration(int seconds);
float getPulseFactor();
void updateLEDsContinuous();
void runEndEffect();
void runEndEffectAt(uint8_t fx, unsigned long age);
uint32_t wheel(uint8_t pos);
void startWebServer();
void handleWebRoot();
void handleWebSave();
String colorToHex(uint32_t c);
uint32_t hexToColor(String hex);
void showBootScreen();
void showCreditsScreen();
void initStars();
void updateStars();
bool checkEasterEgg();
void flashConfirm();
void enterArcade();
void exitArcade();
void drawArcadeMenu();
void initDino();
void updateDino();
void drawDino();
void initPong();
void updatePong(int encDelta, bool btnPressed);
void drawPong();
void initBreakout();
void updateBreakout(int encDelta, bool btnPressed);
void drawBreakout();
void arcadeLEDs();
void gameOverSequence(int score, const char* gameName);
void runInGameCountdown(void (*drawGameFn)());
void loadHighScores();
void saveHighScore(const char* key, int score);
void drawBootMenu();
void drawIconTomate(int cx, int cy);
void drawIconSablier(int cx, int cy);
void drawIconChrono(int cx, int cy);
void drawIconAmpoule(int cx, int cy);
void updateChrono();
void drawChrono();
void updateTimerSimple();
void drawTimerSimple();
void updateAmbiance();

// ======================
// ARCADE — MENU & UTILS
// ======================

void enterArcade()
{
  mode           = MODE_ARCADE;
  currentGame    = GAME_NONE;
  arcadeMenuIndex = 0;
  strip.clear(); strip.show();
  drawArcadeMenu();
}

void exitArcade()
{
  mode        = MODE_SET;
  currentGame = GAME_NONE;
  strip.clear(); strip.show();
  encPosBootMenu = -encoder.read() / 4;
  drawBootMenu();
  strip.clear(); strip.show();
}

void drawArcadeMenu()
{
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);

  // Titre
  display.setCursor(22, 0);
  display.print("* ARCADE *");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  const char* games[] = { "Dino Runner", "Pong vs IA", "Casse-briques" };
  int highScores[]     = { hsDino, hsPong, hsBreakout };

  for (int i = 0; i < ARCADE_GAME_COUNT; i++) {
    int y = 14 + i * 16;
    if (i == arcadeMenuIndex) {
      display.fillRect(0, y - 1, 127, 14, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y + 2);
    display.print(games[i]);
    display.setCursor(88, y + 2);
    display.print("HI:"); display.print(highScores[i]);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 58);
  display.print("Btn=jouer  Long=quitter");
  display.display();

  // LEDs : couleur selon jeu surligné
  uint32_t cols[] = {
    strip.Color(0, 200, 50),    // dino : vert
    strip.Color(50, 100, 255),  // pong : bleu
    strip.Color(255, 100, 0)    // breakout : orange
  };
  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, cols[arcadeMenuIndex]);
  strip.show();
}

void arcadeLEDs()
{
  // Appelée dans le loop quand MODE_ARCADE
  // Pour le dino : on met à jour en continu dans updateDino
  // Pour les autres : géré dans leurs update
}

void gameOverSequence(int score, const char* gameName)
{
  // Récupérer le hi score du jeu concerné
  int hs = 0;
  bool newRecord = false;
  if      (strcmp(gameName, "Dino Runner")   == 0) hs = hsDino;
  else if (strcmp(gameName, "Pong vs IA")    == 0) hs = hsPong;
  else if (strcmp(gameName, "Casse-briques") == 0) hs = hsBreakout;
  newRecord = (score >= hs && score > 0);

  // Flash rouge sur les LEDs
  for (int f = 0; f < 3; f++) {
    for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(255, 0, 0));
    strip.show();
    delay(150);
    strip.clear(); strip.show();
    delay(100);
  }

  // Flash doré si nouveau record
  if (newRecord) {
    for (int f = 0; f < 3; f++) {
      for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(255, 180, 0));
      strip.show();
      delay(150);
      strip.clear(); strip.show();
      delay(100);
    }
  }

  // Ecran game over
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(2);
  display.setCursor(10, 0);
  display.print("GAME OVER");
  display.setTextSize(1);
  display.setCursor(20, 18);
  display.print(gameName);
  display.setCursor(10, 30);
  display.print("Score  : "); display.print(score);
  display.setCursor(10, 40);
  display.print("Record : "); display.print(hs);
  if (newRecord) {
    display.setCursor(10, 52);
    display.print("*NOUVEAU RECORD !*");
  } else {
    display.setCursor(5, 54);
    display.print("Btn = rejouer");
  }
  display.display();

  // Attendre appui bouton
  delay(500);
  while (digitalRead(BTN) == HIGH) {
    server.handleClient();
    delay(10);
  }
  delay(200);

  // Retour menu arcade
  currentGame = GAME_NONE;
  drawArcadeMenu();
}

// ======================
// COUNTDOWN 3-2-1-GO
// ======================
// Countdown discret superposé au jeu (drawGameFn = drawPong ou drawBreakout)
void runInGameCountdown(void (*drawGameFn)())
{
  const char* labels[] = { "3", "2", "1", "GO!" };
  for (int i = 0; i < 4; i++) {
    // Dessiner le jeu en arrière-plan
    drawGameFn();

    // Superposer le chiffre dans un cadre centré discret
    display.setFont(nullptr);
    display.setTextSize(2);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(labels[i], 0, 0, &x1, &y1, &w, &h);
    int cx = (SCREEN_WIDTH  - w) / 2 - x1;
    int cy = (SCREEN_HEIGHT - h) / 2 - y1;

    // Petit fond noir pour lisibilité
    display.fillRect(cx - 4, cy - 4, w + 8, h + 8, SSD1306_BLACK);
    display.drawRect(cx - 5, cy - 5, w + 10, h + 10, SSD1306_WHITE);
    display.setCursor(cx, cy);
    display.print(labels[i]);
    display.display();

    // LEDs : fade in → fade out doux sur les 2 LEDs centrales
    {
      int duration = (i < 3) ? 800 : 400;
      int steps    = 40;
      int stepMs   = duration / steps;
      for (int s = 0; s < steps; s++) {
        // Sinus sur [0..PI] : part de 0, monte à 1, redescend à 0
        float phase = (float)s / (steps - 1); // 0.0 → 1.0
        float bri   = sin(phase * PI);         // 0 → 1 → 0
        uint8_t val = (uint8_t)(bri * 100);    // max 100/255
        strip.setPixelColor(5, strip.Color(val, val, val));
        strip.setPixelColor(6, strip.Color(val, val, val));
        strip.show();
        delay(stepMs);
      }
      strip.setPixelColor(5, 0);
      strip.setPixelColor(6, 0);
      strip.show();
    }
  }
}

// ======================
// DINO RUNNER
// ======================

// Sprite dino 8x12 (bitmap simplifié)
const uint8_t dinoSprite[12] = {
  0b00111000,  // tete
  0b00111100,
  0b00111110,
  0b11111100,  // corps
  0b11111100,
  0b01111100,
  0b00111100,
  0b00101000,  // pattes
  0b00111000,
  0b01010000,
  0b01000000,
  0b11000000,
};

// Sprite cactus simple (6px large)
void drawCactus(int x, int h)
{
  // Tronc central
  display.fillRect(x + 2, DINO_GROUND_Y - h, 2, h, SSD1306_WHITE);
  // Branches
  if (h > 10) {
    display.fillRect(x, DINO_GROUND_Y - h + 3, 2, 5, SSD1306_WHITE);
    display.fillRect(x + 4, DINO_GROUND_Y - h + 5, 2, 4, SSD1306_WHITE);
  } else {
    display.fillRect(x, DINO_GROUND_Y - h + 2, 2, 3, SSD1306_WHITE);
  }
}

void initDino()
{
  dino.y        = DINO_GROUND_Y - DINO_H;
  dino.vy       = 0;
  dino.onGround = true;
  dino.speed    = 1.8f;
  dino.score    = 0;
  dino.dead     = false;
  dino.lastFrame = millis();

  // Init cactus
  dino.cacti[0].x = 140;
  dino.cacti[0].h = random(CACTUS_H_MIN, CACTUS_H_MAX);
  dino.cacti[0].active = true;
  dino.cacti[1].x = 220;
  dino.cacti[1].h = random(CACTUS_H_MIN, CACTUS_H_MAX);
  dino.cacti[1].active = true;
}

void updateDino()
{
  if (dino.dead) return;

  unsigned long now = millis();
  if (now - dino.lastFrame < 30) return; // ~33fps
  float dt = (now - dino.lastFrame) / 1000.0f;
  dino.lastFrame = now;

  // Saut : détection front descendant bouton
  static bool lastBtnDino = HIGH;
  bool btnNow = digitalRead(BTN);
  if (lastBtnDino == HIGH && btnNow == LOW && dino.onGround) {
    dino.vy = DINO_JUMP_VY;
    dino.onGround = false;
  }
  lastBtnDino = btnNow;

  // Physique
  dino.vy += DINO_GRAVITY;
  dino.y  += dino.vy;

  if (dino.y >= DINO_GROUND_Y - DINO_H) {
    dino.y       = DINO_GROUND_Y - DINO_H;
    dino.vy      = 0;
    dino.onGround = true;
  }

  // Cactus
  for (int c = 0; c < CACTUS_COUNT; c++) {
    dino.cacti[c].x -= dino.speed;
    if (dino.cacti[c].x < -CACTUS_W) {
      dino.cacti[c].x = 128 + random(40, 80);
      dino.cacti[c].h = random(CACTUS_H_MIN, CACTUS_H_MAX);
      dino.score++;
      // Accélération progressive
      dino.speed = 1.8f + dino.score * 0.08f;
      if (dino.speed > 4.5f) dino.speed = 4.5f;
    }

    // Collision AABB
    int cx = (int)dino.cacti[c].x;
    int cy = DINO_GROUND_Y - dino.cacti[c].h;
    int dy = (int)dino.y;
    bool collideX = (DINO_X + DINO_W - 2) > cx && (DINO_X + 2) < (cx + CACTUS_W);
    bool collideY = (dy + DINO_H)          > cy && dy < DINO_GROUND_Y;
    if (collideX && collideY) {
      dino.dead = true;
      if (dino.score > hsDino) {
        hsDino = dino.score;
        saveHighScore("hsDino", hsDino);
      }
      gameOverSequence(dino.score, "Dino Runner");
      return;
    }
  }

  drawDino();
}

void drawDino()
{
  display.clearDisplay();

  // Sol
  display.drawLine(0, DINO_GROUND_Y, 127, DINO_GROUND_Y, SSD1306_WHITE);

  // Score
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(80, 0);
  display.print("Score:");
  display.print(dino.score);

  // Dino (sprite bitmap)
  int dy = (int)dino.y;
  for (int row = 0; row < DINO_H; row++) {
    uint8_t rowData = dinoSprite[row];
    for (int col = 0; col < DINO_W; col++) {
      if (rowData & (0x80 >> col))
        display.drawPixel(DINO_X + col, dy + row, SSD1306_WHITE);
    }
  }

  // Cactus
  for (int c = 0; c < CACTUS_COUNT; c++)
    drawCactus((int)dino.cacti[c].x, dino.cacti[c].h);

  display.display();

  // LEDs : score → vert au début, rouge à haute vitesse
  float ratio = (dino.speed - 1.8f) / (4.5f - 1.8f);
  uint8_t r = (uint8_t)(ratio * 255);
  uint8_t g = (uint8_t)((1.0f - ratio) * 255);
  int ledsOn = min(LED_COUNT, 1 + dino.score / 2);
  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, i < ledsOn ? strip.Color(r, g, 0) : 0);
  strip.show();
}

// ======================
// PONG VS IA
// ======================

void initPong()
{
  pong.ballX   = 64; pong.ballY = 32;
  pong.ballVX  = 1.5f; pong.ballVY = 1.0f;
  pong.playerY = 24;
  pong.aiY     = 24;
  pong.scorePlayer = 0; pong.scoreAI = 0;
  pong.paused  = true;
  pong.lastFrame = millis();
}

void updatePong(int encDelta, bool btnPressed)
{
  // Lire aussi le bouton physique directement (front descendant)
  static bool lastBtnPong = HIGH;
  bool btnNow = digitalRead(BTN);
  bool btnEdge = (lastBtnPong == HIGH && btnNow == LOW);
  lastBtnPong = btnNow;
  if (btnEdge) btnPressed = true;

  unsigned long now = millis();
  if (now - pong.lastFrame < 25) return; // ~40fps
  pong.lastFrame = now;

  // Lancer balle
  if (pong.paused && btnPressed) {
    runInGameCountdown(drawPong);
    encPosPong = -encoder.read() / 4; // reset encodeur après countdown
    pong.lastFrame = millis();
    pong.paused = false;
    pong.ballVX = (pong.scorePlayer > pong.scoreAI) ? -1.5f : 1.5f;
    pong.ballVY = 1.0f;
  }
  if (pong.paused) { drawPong(); return; }

  // Raquette joueur (droite) — encodeur
  pong.playerY += encDelta * 2.5f;
  pong.playerY  = constrain(pong.playerY, 0, 64 - PONG_PADDLE_H);

  // IA (gauche) — suit la balle avec un léger délai
  float aiCenter = pong.aiY + PONG_PADDLE_H / 2.0f;
  float aiSpeed  = 1.2f + (pong.scorePlayer + pong.scoreAI) * 0.05f;
  if (aiSpeed > 2.5f) aiSpeed = 2.5f;
  if (aiCenter < pong.ballY - 2) pong.aiY += aiSpeed;
  else if (aiCenter > pong.ballY + 2) pong.aiY -= aiSpeed;
  pong.aiY = constrain(pong.aiY, 0, 64 - PONG_PADDLE_H);

  // Mouvement balle
  pong.ballX += pong.ballVX;
  pong.ballY += pong.ballVY;

  // Rebond haut/bas
  if (pong.ballY <= 0 || pong.ballY >= 64 - PONG_BALL_SIZE)
    pong.ballVY = -pong.ballVY;

  // Collision raquette joueur (droite x=121)
  if (pong.ballX >= 121 - PONG_BALL_SIZE &&
      pong.ballY + PONG_BALL_SIZE >= pong.playerY &&
      pong.ballY <= pong.playerY + PONG_PADDLE_H) {
    pong.ballVX = -abs(pong.ballVX) * 1.05f;
    float rel = (pong.ballY - pong.playerY) / (float)PONG_PADDLE_H - 0.5f;
    pong.ballVY = rel * 3.5f;
  }

  // Collision raquette IA (gauche x=4)
  if (pong.ballX <= 4 + PONG_PADDLE_W &&
      pong.ballY + PONG_BALL_SIZE >= pong.aiY &&
      pong.ballY <= pong.aiY + PONG_PADDLE_H) {
    pong.ballVX = abs(pong.ballVX) * 1.05f;
    float rel = (pong.ballY - pong.aiY) / (float)PONG_PADDLE_H - 0.5f;
    pong.ballVY = rel * 3.5f;
  }

  // Limiter vitesse max
  if (pong.ballVX >  4.0f) pong.ballVX =  4.0f;
  if (pong.ballVX < -4.0f) pong.ballVX = -4.0f;

  // Point marqué
  if (pong.ballX < 0) {
    pong.scorePlayer++;
    pong.paused = true;
    pong.ballX = 64; pong.ballY = 32;
    if (pong.scorePlayer >= PONG_WIN_SCORE) {
      if (pong.scorePlayer > hsPong) {
        hsPong = pong.scorePlayer;
        saveHighScore("hsPong", hsPong);
      }
      for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, wheel(i * 21));
      strip.show();
      display.clearDisplay();
      display.setFont(nullptr); display.setTextSize(2);
      display.setCursor(8, 8); display.print("VICTOIRE!");
      display.setTextSize(1);
      display.setCursor(10, 30); display.print("Tu as battu l'IA !");
      display.setCursor(10, 42); display.print("Record: "); display.print(hsPong);
      display.display();
      delay(2500);
      initPong(); currentGame = GAME_NONE; drawArcadeMenu(); return;
    }
  }
  if (pong.ballX > 128) {
    pong.scoreAI++;
    pong.paused = true;
    pong.ballX = 64; pong.ballY = 32;
    if (pong.scoreAI >= PONG_WIN_SCORE) {
      gameOverSequence(pong.scorePlayer, "Pong vs IA");
      return;
    }
  }

  drawPong();
}

void drawPong()
{
  display.clearDisplay();

  // Ligne centrale pointillée
  for (int y = 0; y < 64; y += 5) display.drawPixel(63, y, SSD1306_WHITE);

  // Scores
  display.setFont(nullptr); display.setTextSize(1);
  display.setCursor(20, 0); display.print(pong.scoreAI);
  display.setCursor(100, 0); display.print(pong.scorePlayer);

  // Raquettes
  display.fillRect(4,   (int)pong.aiY,     PONG_PADDLE_W, PONG_PADDLE_H, SSD1306_WHITE);
  display.fillRect(121, (int)pong.playerY, PONG_PADDLE_W, PONG_PADDLE_H, SSD1306_WHITE);

  // Balle
  display.fillRect((int)pong.ballX, (int)pong.ballY, PONG_BALL_SIZE, PONG_BALL_SIZE, SSD1306_WHITE);

  if (pong.paused) {
    display.setCursor(30, 28); display.print("Btn=lancer");
  }

  display.display();

  // LEDs : moitié gauche = score IA, moitié droite = score joueur
  int ledPlayer = map(pong.scorePlayer, 0, PONG_WIN_SCORE, 0, LED_COUNT / 2);
  int ledAI     = map(pong.scoreAI,     0, PONG_WIN_SCORE, 0, LED_COUNT / 2);
  for (int i = 0; i < LED_COUNT; i++) {
    if (i < ledPlayer)
      strip.setPixelColor(i, strip.Color(0, 180, 255));     // joueur : bleu
    else if (i >= LED_COUNT - ledAI)
      strip.setPixelColor(i, strip.Color(255, 60, 0));       // IA : rouge
    else
      strip.setPixelColor(i, 0);
  }
  strip.show();
}

// ======================
// CASSE-BRIQUES
// ======================

void initBreakout()
{
  // Init briques
  for (int r = 0; r < BRK_ROWS; r++)
    for (int c = 0; c < BRK_COLS; c++)
      brk.bricks[r][c] = true;

  brk.paddleX  = (128 - BRK_PADDLE_W) / 2;
  brk.ballX    = 64; brk.ballY = BRK_PADDLE_Y - BRK_BALL_R * 2 - 1;
  brk.ballVX   = 1.5f; brk.ballVY = -1.5f;
  brk.score    = 0;
  brk.lives    = 3;
  brk.launched = false;
  brk.lastFrame = millis();
}

void updateBreakout(int encDelta, bool btnPressed)
{
  // Lire aussi le bouton physique directement (front descendant)
  static bool lastBtnBrk = HIGH;
  bool btnNow = digitalRead(BTN);
  bool btnEdge = (lastBtnBrk == HIGH && btnNow == LOW);
  lastBtnBrk = btnNow;
  if (btnEdge) btnPressed = true;

  unsigned long now = millis();
  if (now - brk.lastFrame < 25) return;
  brk.lastFrame = now;

  // Raquette
  brk.paddleX += encDelta * 3.0f;
  brk.paddleX  = constrain(brk.paddleX, 0, 128 - BRK_PADDLE_W);

  // Lancer balle
  if (!brk.launched && btnPressed) {
    runInGameCountdown(drawBreakout);
    encPosBreakout = -encoder.read() / 4; // reset encodeur après countdown
    brk.lastFrame = millis();
    brk.launched = true;
    brk.ballVX   = 1.5f;
    brk.ballVY   = -2.0f;
  }

  if (!brk.launched) {
    // Balle colle à la raquette
    brk.ballX = brk.paddleX + BRK_PADDLE_W / 2;
    brk.ballY = BRK_PADDLE_Y - BRK_BALL_R - 1;
    drawBreakout(); return;
  }

  // Mouvement
  brk.ballX += brk.ballVX;
  brk.ballY += brk.ballVY;

  // Rebond murs gauche/droite
  if (brk.ballX <= BRK_BALL_R || brk.ballX >= 128 - BRK_BALL_R)
    brk.ballVX = -brk.ballVX;
  // Rebond plafond
  if (brk.ballY <= BRK_BALL_R)
    brk.ballVY = abs(brk.ballVY);

  // Collision raquette
  if (brk.ballY + BRK_BALL_R >= BRK_PADDLE_Y &&
      brk.ballY + BRK_BALL_R <= BRK_PADDLE_Y + BRK_PADDLE_H &&
      brk.ballX >= brk.paddleX - BRK_BALL_R &&
      brk.ballX <= brk.paddleX + BRK_PADDLE_W + BRK_BALL_R) {
    brk.ballVY = -abs(brk.ballVY);
    float rel  = (brk.ballX - (brk.paddleX + BRK_PADDLE_W / 2.0f)) / (BRK_PADDLE_W / 2.0f);
    brk.ballVX = rel * 2.5f;
  }

  // Balle perdue
  if (brk.ballY > 68) {
    brk.lives--;
    if (brk.lives <= 0) {
      gameOverSequence(brk.score, "Casse-briques");
      return;
    }
    brk.launched = false;
    brk.ballX = brk.paddleX + BRK_PADDLE_W / 2;
    brk.ballY = BRK_PADDLE_Y - BRK_BALL_R - 1;
  }

  // Collision briques
  int totalBricks = 0;
  for (int r = 0; r < BRK_ROWS; r++) {
    for (int c = 0; c < BRK_COLS; c++) {
      if (!brk.bricks[r][c]) continue;
      totalBricks++;
      int bx = c * (BRK_BRICK_W + BRK_BRICK_GAP) + 1;
      int by = r * (BRK_BRICK_H + BRK_BRICK_GAP) + 8;
      if (brk.ballX + BRK_BALL_R > bx && brk.ballX - BRK_BALL_R < bx + BRK_BRICK_W &&
          brk.ballY + BRK_BALL_R > by && brk.ballY - BRK_BALL_R < by + BRK_BRICK_H) {
        brk.bricks[r][c] = false;
        brk.score++;
        brk.ballVY = -brk.ballVY;
        // Accélération légère
        float spd = sqrt(brk.ballVX*brk.ballVX + brk.ballVY*brk.ballVY);
        if (spd < 4.0f) { brk.ballVX *= 1.04f; brk.ballVY *= 1.04f; }
      }
    }
  }

  // Victoire : toutes les briques cassées
  if (totalBricks == 0) {
    for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, wheel(i * 21));
    strip.show();
    display.clearDisplay();
    display.setFont(nullptr); display.setTextSize(2);
    display.setCursor(8, 8); display.print("BRAVO !");
    display.setTextSize(1);
    if (brk.score > hsBreakout) {
      hsBreakout = brk.score;
      saveHighScore("hsBreakout", hsBreakout);
    }
    display.setCursor(5, 28); display.print("Score : "); display.print(brk.score);
    display.setCursor(5, 40); display.print("Record: "); display.print(hsBreakout);
    display.setCursor(5, 52); display.print("Toutes cassees !");
    display.display();
    delay(2500);
    initBreakout(); currentGame = GAME_NONE; drawArcadeMenu(); return;
  }

  drawBreakout();
}

void drawBreakout()
{
  display.clearDisplay();
  display.setFont(nullptr); display.setTextSize(1);

  // Score + vies
  display.setCursor(0, 0); display.print("Score:"); display.print(brk.score);
  display.setCursor(90, 0);
  for (int l = 0; l < brk.lives; l++) display.print("*");

  // Briques
  for (int r = 0; r < BRK_ROWS; r++) {
    for (int c = 0; c < BRK_COLS; c++) {
      if (!brk.bricks[r][c]) continue;
      int bx = c * (BRK_BRICK_W + BRK_BRICK_GAP) + 1;
      int by = r * (BRK_BRICK_H + BRK_BRICK_GAP) + 8;
      display.drawRect(bx, by, BRK_BRICK_W, BRK_BRICK_H, SSD1306_WHITE);
    }
  }

  // Raquette
  display.fillRect((int)brk.paddleX, BRK_PADDLE_Y, BRK_PADDLE_W, BRK_PADDLE_H, SSD1306_WHITE);

  // Balle
  display.fillCircle((int)brk.ballX, (int)brk.ballY, BRK_BALL_R, SSD1306_WHITE);

  if (!brk.launched) {
    display.setCursor(20, 52); display.print("Btn = lancer !");
  }

  display.display();

  // LEDs : briques restantes
  int bricksLeft = 0;
  for (int r = 0; r < BRK_ROWS; r++)
    for (int c = 0; c < BRK_COLS; c++)
      if (brk.bricks[r][c]) bricksLeft++;
  int total = BRK_ROWS * BRK_COLS;
  int ledsOn = map(bricksLeft, 0, total, 0, LED_COUNT);
  float ratio = 1.0f - (float)bricksLeft / total;
  for (int i = 0; i < LED_COUNT; i++) {
    if (i < ledsOn)
      strip.setPixelColor(i, strip.Color((uint8_t)(ratio*255), (uint8_t)((1-ratio)*200), 50));
    else
      strip.setPixelColor(i, 0);
  }
  strip.show();
}


// ======================
// FEEDBACK VALIDATION
// Flash vert sur le ruban LED + coche sur l'OLED
// ======================
void flashConfirm()
{
  // --- Ruban LED : flash vert 2 fois ---
  for (int f = 0; f < 2; f++) {
    for (int i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, strip.Color(0, 255, 0)); // vert pur
    strip.show();
    delay(80);
    strip.clear();
    strip.show();
    delay(60);
  }

  // --- OLED : coche centrée x3 ---
  display.clearDisplay();

  int cx = SCREEN_WIDTH  / 2; // 64
  int cy = 20;                 // un peu au dessus du centre pour laisser place au texte

  // Coche dessinée avec drawLine épaisse (x3 : segments de 3px)
  // Trait gauche : descend vers le bas-droite
  for (int t = 0; t < 3; t++) {
    for (int i = 0; i < 10; i++)
      display.drawPixel(cx - 14 + i + t, cy + i, SSD1306_WHITE);
  }
  // Trait droit : remonte vers le haut-droite
  for (int t = 0; t < 3; t++) {
    for (int i = 0; i < 18; i++)
      display.drawPixel(cx - 4 + i + t, cy + 9 - i, SSD1306_WHITE);
  }

  // "OK !" en taille 2 sous la coche
  display.setFont(nullptr);
  display.setTextSize(2);
  display.setCursor(cx - 18, cy + 22);
  display.print("OK !");

  display.display();
  delay(500);
}

// ======================
// SETUP
// ======================
void setup()
{
  Serial.begin(115200);
  delay(300);

  pinMode(BTN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAIL");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  strip.begin();
  strip.show();

  loadConfig();
  loadHighScores();
  strip.setBrightness(cfg.brightness);

  initStars();
  showBootScreen();

  // Démarrage WiFi AP + serveur web
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  startWebServer();

  mode           = MODE_BOOT_MENU;
  bootMenuIndex  = 0;
  encPosBootMenu = -encoder.read() / 4;
  drawBootMenu();
  updateLEDs();
}

// ======================
// LOOP
// ======================
void loop()
{
  server.handleClient();

  // Menu boot
  if (mode == MODE_BOOT_MENU) {
    long pos   = -encoder.read() / 4;
    int  delta = (int)(pos - encPosBootMenu);
    if (delta != 0) {
      encPosBootMenu = pos;
      bootMenuIndex  = (bootMenuIndex + delta + BOOT_MENU_COUNT) % BOOT_MENU_COUNT;
      drawBootMenu();
    }
    static bool lastBtnBoot = HIGH;
    bool btnNow = digitalRead(BTN);
    if (lastBtnBoot == HIGH && btnNow == LOW) {
      delay(30);
      if (digitalRead(BTN) == LOW) {
        while (digitalRead(BTN) == LOW) delay(5);
        switch (bootMenuIndex) {
          case 0: // Pomodoro
            mode = MODE_SET;
            lastEncoderPos = -encoder.read() / 4;
            drawScreen(); updateLEDs();
            break;
          case 1: // Minuteur simple
            timerSimpleMinutes   = 5;
            timerSimpleRemaining = timerSimpleMinutes * 60;
            timerSimpleRunning   = false;
            mode = MODE_TIMER_SIMPLE;
            lastEncoderPos = -encoder.read() / 4;
            drawTimerSimple();
            break;
          case 2: // Chrono
            mode = MODE_CHRONO;
            chronoElapsed = 0; chronoRunning = false; chronoStart = 0;
            lastEncoderPos = -encoder.read() / 4;
            drawChrono();
            break;
          case 3: // Ambiance
            mode = MODE_AMBIANCE;
            ambianceEffect = 0;
            strip.clear(); strip.show();
            break;
        }
      }
    }
    lastBtnBoot = btnNow;
    return;
  }

  // Modes custom
  if (mode == MODE_TIMER_SIMPLE) { updateTimerSimple(); return; }
  if (mode == MODE_CHRONO)       { updateChrono();      return; }
  if (mode == MODE_AMBIANCE)     { updateAmbiance();    return; }

  handleEncoder();
  handleButton();

  if (mode == MODE_DONE) {
    runEndEffect();
  } else if (mode == MODE_CREDITS) {
    showCreditsScreen();
    // Arc-en-ciel en arrière-plan sur les LEDs
    unsigned long age = millis() - creditsStartMs;
    runEndEffectAt(FX_RAINBOW, age);
    if (age > CREDITS_DURATION) {
      mode = MODE_SET;
      strip.clear(); strip.show();
      drawScreen(); updateLEDs();
    }
  } else if (mode == MODE_ARCADE) {
    if (currentGame == GAME_DINO) {
      updateDino();
    } else if (currentGame == GAME_PONG) {
      long pos = -encoder.read() / 4;
      int delta = (int)(pos - encPosPong);
      encPosPong = pos;
      updatePong(delta, false);
    } else if (currentGame == GAME_BREAKOUT) {
      long pos = -encoder.read() / 4;
      int delta = (int)(pos - encPosBreakout);
      encPosBreakout = pos;
      updateBreakout(delta, false);
    }
    // Menu arcade : géré par handleEncoder/handleButton
  } else {
    updateTimer();
    // Mise à jour continue des LEDs pour le pulse (en WORK et BREAK)
    if ((mode == MODE_WORK || mode == MODE_BREAK) && menuState == MENU_NONE) {
      updateLEDsContinuous();
    }
  }
}

// ======================
// PERSISTANCE NVS
// ======================
void loadConfig()
{
  prefs.begin("timer", false);
  cfg.colorWork  = prefs.getUInt("cWork",    cfgDefault.colorWork);
  cfg.colorBreak = prefs.getUInt("cBreak",   cfgDefault.colorBreak);
  cfg.colorSet   = prefs.getUInt("cSet",     cfgDefault.colorSet);
  cfg.fxEnd      = prefs.getUChar("fxEnd",   cfgDefault.fxEnd);
  cfg.brightness = prefs.getUChar("bright",  cfgDefault.brightness);
  cfg.pulseMin   = prefs.getFloat("pulseMin",cfgDefault.pulseMin);
  cfg.pulseMax   = prefs.getFloat("pulseMax",cfgDefault.pulseMax);
  cfg.fontIndex  = prefs.getUChar("fontIdx", cfgDefault.fontIndex);
  prefs.end();
}

void saveConfig()
{
  prefs.begin("timer", false);
  prefs.putUInt("cWork",    cfg.colorWork);
  prefs.putUInt("cBreak",   cfg.colorBreak);
  prefs.putUInt("cSet",     cfg.colorSet);
  prefs.putUChar("fxEnd",   cfg.fxEnd);
  prefs.putUChar("bright",  cfg.brightness);
  prefs.putFloat("pulseMin",cfg.pulseMin);
  prefs.putFloat("pulseMax",cfg.pulseMax);
  prefs.putUChar("fontIdx", cfg.fontIndex);
  prefs.end();
}

void loadHighScores()
{
  prefs.begin("scores", true);
  hsDino     = prefs.getInt("hsDino",     0);
  hsPong     = prefs.getInt("hsPong",     0);
  hsBreakout = prefs.getInt("hsBreakout", 0);
  prefs.end();
}

void saveHighScore(const char* key, int score)
{
  prefs.begin("scores", false);
  prefs.putInt(key, score);
  prefs.end();
}

// ======================
// PULSE LED (par seconde écoulée)
// Le pulse suit millis()%1000 pour battre à chaque seconde
// Descend vers pulseMin au milieu de chaque seconde, remonte à pulseMax
// ======================
float getPulseFactor()
{
  // En MODE_SET : pas de pulse, luminosité fixe
  if (mode == MODE_SET) return cfg.pulseMax;

  unsigned long t = millis() % 1000;
  float phase     = (float)t / 1000.0f;
  // sin décalé : commence à pulseMax, descend à pulseMin vers t=500ms, remonte
  float val = (sin(phase * TWO_PI + PI / 2.0f) + 1.0f) / 2.0f;
  return cfg.pulseMin + val * (cfg.pulseMax - cfg.pulseMin);
}

// ======================
// ENCODEUR
// ======================
void handleEncoder()
{
  long pos = -encoder.read() / 4;
  int delta = pos - lastEncoderPos;

  if (delta == 0) return;
  lastEncoderPos = pos;

  if (menuState != MENU_NONE) {
    menuNavigate(delta);
    return;
  }

  // En MODE_ARCADE : seulement le menu (les jeux lisent l'encodeur dans le loop)
  if (mode == MODE_ARCADE) {
    if (currentGame == GAME_NONE && delta != 0) {
      arcadeMenuIndex = (arcadeMenuIndex + delta + ARCADE_GAME_COUNT) % ARCADE_GAME_COUNT;
      drawArcadeMenu();
    }
    return;
  }

  // En MODE_SET : ajuster workMinutes
  if (mode == MODE_SET) {
    workMinutes += delta * STEP_MINUTES;
    if (workMinutes < STEP_MINUTES) workMinutes = STEP_MINUTES;
    if (workMinutes > 720)          workMinutes = 720;
    workRemainingSeconds = workMinutes * 60;
    remainingSeconds     = workRemainingSeconds;
    drawScreen();
    updateLEDs();
  }
}

// ======================
// BOUTON (court/long)
// ======================
void handleButton()
{
  bool state = digitalRead(BTN);

  // Front descendant : début d'appui
  if (lastButtonState == HIGH && state == LOW) {
    btnPressTime   = millis();
    btnLongHandled = false;
  }

  // Maintien > 800ms → menu (sauf en MODE_CREDITS)
  if (state == LOW && !btnLongHandled) {
    if (millis() - btnPressTime > 800) {
      btnLongHandled = true;
      if (mode == MODE_CREDITS) {
        // Fermer les crédits
        mode = MODE_SET;
        strip.clear(); strip.show();
        drawScreen(); updateLEDs();
      } else if (mode == MODE_ARCADE) {
        if (currentGame != GAME_NONE) {
          // Quitter le jeu en cours → retour menu arcade
          currentGame = GAME_NONE;
          drawArcadeMenu();
        } else {
          // Quitter l'arcade → retour timer
          exitArcade();
        }
      } else if (menuState == MENU_NONE) {
        enterMenu(MENU_MAIN);
      } else {
        exitMenu();
      }
    }
  }

  // Relâchement : appui court
  if (lastButtonState == LOW && state == HIGH) {
    unsigned long pressDuration = millis() - btnPressTime;
    if (!btnLongHandled && pressDuration < 800) {

      // Easter egg compté EN PREMIER, quel que soit le mode/menu
      bool eggJustTriggered = checkEasterEgg();
      if (eggJustTriggered) {
        // L'easter egg vient de s'ouvrir (crédits ou arcade), on ne fait rien d'autre
      } else if (mode == MODE_ARCADE) {
        if (currentGame == GAME_NONE) {
          // Menu arcade : bouton court = lancer le jeu sélectionné
          // Reset encodeur dédié selon le jeu
          switch (arcadeMenuIndex) {
            case 0:
              initDino();
              currentGame = GAME_DINO;
              break;
            case 1:
              encPosPong = -encoder.read() / 4;
              initPong();
              currentGame = GAME_PONG;
              break;
            case 2:
              encPosBreakout = -encoder.read() / 4;
              initBreakout();
              currentGame = GAME_BREAKOUT;
              break;
          }
        }
        // Les actions in-game (saut, lancer) sont gérées localement dans les update
      } else if (mode == MODE_CREDITS) {
        // Fermer les crédits
        mode = MODE_SET;
        strip.clear(); strip.show();
        drawScreen(); updateLEDs();
      } else if (menuState != MENU_NONE) {
        menuSelect();
      } else {
        if (mode == MODE_SET) {
          mode                  = MODE_WORK;
          workRemainingSeconds  = workMinutes * 60;
          remainingSeconds      = workRemainingSeconds;
          workElapsedSinceBreak = 0;
          totalWorkSeconds      = 0;
          lastTick              = millis();
        } else if (mode == MODE_DONE) {
          mode                 = MODE_SET;
          workRemainingSeconds = workMinutes * 60;
          remainingSeconds     = workRemainingSeconds;
        } else {
          mode                 = MODE_SET;
          workRemainingSeconds = workMinutes * 60;
          remainingSeconds     = workRemainingSeconds;
        }
        drawScreen();
        updateLEDs();
      }
      // Pas de delay() ici — il tuerait la détection des clics rapides
    }
  }

  lastButtonState = state;
}

// ======================
// TIMER
// ======================
void updateTimer()
{
  if (mode == MODE_SET) return;

  if (millis() - lastTick < 1000) return;
  lastTick = millis();

  if (mode == MODE_WORK) {
    workRemainingSeconds--;
    remainingSeconds = workRemainingSeconds;
    totalWorkSeconds++;
    workElapsedSinceBreak++;

    if (workElapsedSinceBreak >= BREAK_INTERVAL_SECONDS && workRemainingSeconds > 0) {
      mode = MODE_BREAK;
      breakRemainingSeconds = BREAK_MINUTES * 60;
      remainingSeconds      = breakRemainingSeconds;
      workElapsedSinceBreak = 0;
    }

    if (workRemainingSeconds <= 0) {
      mode        = MODE_DONE;
      doneStartMs = millis();
      remainingSeconds = 0;
    }
  }
  else if (mode == MODE_BREAK) {
    breakRemainingSeconds--;
    remainingSeconds = breakRemainingSeconds;

    if (breakRemainingSeconds <= 0) {
      mode = MODE_WORK;
      remainingSeconds = workRemainingSeconds;
    }
  }

  if (menuState == MENU_NONE) {
    drawScreen();
    updateLEDs();
  }
}

// ======================
// AFFICHAGE OLED
// ======================
void drawScreen()
{
  if (menuState != MENU_NONE) {
    drawMenu();
    return;
  }

  // Ces modes gèrent leur propre affichage
  if (mode == MODE_BOOT_MENU || mode == MODE_TIMER_SIMPLE ||
      mode == MODE_CHRONO    || mode == MODE_AMBIANCE) return;

  display.clearDisplay();

  if (mode == MODE_DONE) {
    display.setTextSize(2);
    display.setCursor(20, 20);
    display.print("TERMINE !");
    display.display();
    return;
  }

  String timeStr = formatDuration(remainingSeconds);

  // Appliquer la police choisie
  const GFXfont* fnt = getFont(cfg.fontIndex);
  display.setFont(fnt);
  display.setTextSize(fontTextSize[cfg.fontIndex]);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);

  // Centrage adapté : les polices GFX custom ont une baseline différente
  int x = (SCREEN_WIDTH  - w) / 2 - x1;
  int y = (SCREEN_HEIGHT - h) / 2 - y1;

  display.setCursor(x, y);
  display.print(timeStr);

  // Indicateur de mode en haut à gauche (police builtin taille 1)
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(0, 0);
  if      (mode == MODE_WORK)  display.print("WORK");
  else if (mode == MODE_BREAK) display.print("PAUSE");
  else                         display.print("SET");

  display.display();
}

// ======================
// MENU OLED
// ======================
void enterMenu(MenuState s)
{
  menuState = s;
  menuIndex = 0;
  menuEnterEncoder = -encoder.read() / 4;
  lastEncoderPos   = menuEnterEncoder;

  // LEDs couleur "config" pendant toute la navigation dans les menus
  // (sauf menus couleur/FX qui ont leur propre preview)
  if (s == MENU_MAIN || s == MENU_BRIGHT || s == MENU_FONT) {
    previewColorOnStrip(cfg.colorSet);
  }

  drawMenu();
}

void exitMenu()
{
  menuState = MENU_NONE;
  saveConfig();
  strip.setBrightness(cfg.brightness);
  drawScreen();
  updateLEDs();
}

void menuNavigate(int delta)
{
  int maxItems = 0;

  switch (menuState) {
    case MENU_MAIN:        maxItems = MAIN_MENU_COUNT;  break;
    case MENU_COLOR_WORK:
    case MENU_COLOR_BREAK:
    case MENU_COLOR_SET:   maxItems = PRESET_COLOR_COUNT; break;
    case MENU_FX:          maxItems = FX_COUNT;          break;
    case MENU_BRIGHT:
      cfg.brightness = constrain((int)cfg.brightness + delta * 10, 10, 255);
      strip.setBrightness(cfg.brightness);
      previewColorOnStrip(mode == MODE_WORK ? cfg.colorWork :
                          mode == MODE_BREAK ? cfg.colorBreak : cfg.colorSet);
      drawMenu();
      return;
    case MENU_FONT:
      cfg.fontIndex = (cfg.fontIndex + delta + FONT_COUNT) % FONT_COUNT;
      drawMenu(); // preview sur l'écran OLED
      return;
    default: break;
  }

  if (maxItems > 0) {
    menuIndex = (menuIndex + delta + maxItems) % maxItems;
  }

  // Preview couleur en temps réel dans les menus couleur
  if (menuState == MENU_COLOR_WORK || menuState == MENU_COLOR_BREAK || menuState == MENU_COLOR_SET) {
    previewColorOnStrip(presetColors[menuIndex]);
  }

  // Preview effet en temps réel dans le menu FX
  if (menuState == MENU_FX) {
    previewFxOnStrip(menuIndex, millis());
  }

  drawMenu();
}

void menuSelect()
{
  switch (menuState) {
    case MENU_MAIN:
      switch (menuIndex) {
        case 0: enterMenu(MENU_COLOR_WORK);  break;
        case 1: enterMenu(MENU_COLOR_BREAK); break;
        case 2: enterMenu(MENU_COLOR_SET);   break;
        case 3: enterMenu(MENU_FX);          break;
        case 4: enterMenu(MENU_BRIGHT);      break;
        case 5: enterMenu(MENU_FONT);        break;
        case 6:
          // Afficher infos portail web
          display.clearDisplay();
          display.setFont(nullptr);
          display.setTextSize(1);
          display.setCursor(0, 0);
          display.print("WiFi: ");
          display.println(AP_SSID);
          display.print("Pass: ");
          display.println(AP_PASS);
          display.print("IP: ");
          display.println(WiFi.softAPIP().toString());
          display.display();
          delay(3000);
          drawMenu();
          break;
        case 7: exitMenu(); break;
      }
      break;

    case MENU_COLOR_WORK:
      cfg.colorWork = presetColors[menuIndex];
      flashConfirm();
      enterMenu(MENU_MAIN);
      break;
    case MENU_COLOR_BREAK:
      cfg.colorBreak = presetColors[menuIndex];
      flashConfirm();
      enterMenu(MENU_MAIN);
      break;
    case MENU_COLOR_SET:
      cfg.colorSet = presetColors[menuIndex];
      flashConfirm();
      enterMenu(MENU_MAIN);
      break;

    case MENU_FX:
      cfg.fxEnd = menuIndex;
      flashConfirm();
      enterMenu(MENU_MAIN);
      break;

    case MENU_BRIGHT:
      flashConfirm();
      enterMenu(MENU_MAIN);
      break;

    case MENU_FONT:
      flashConfirm();
      enterMenu(MENU_MAIN);
      break;

    default:
      break;
  }
}

void drawMenu()
{
  display.clearDisplay();
  display.setTextSize(1);

  switch (menuState) {
    case MENU_MAIN: {
      display.setCursor(0, 0);
      display.print("> MENU CONFIG");
      display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      int startIdx = max(0, menuIndex - 2);
      for (int i = 0; i < 5 && (startIdx + i) < MAIN_MENU_COUNT; i++) {
        int idx = startIdx + i;
        display.setCursor(4, 12 + i * 10);
        if (idx == menuIndex) display.print(">");
        display.setCursor(12, 12 + i * 10);
        display.print(mainMenuItems[idx]);
      }
      break;
    }

    case MENU_COLOR_WORK:
    case MENU_COLOR_BREAK:
    case MENU_COLOR_SET: {
      const char* title = (menuState == MENU_COLOR_WORK) ? "Couleur WORK" :
                          (menuState == MENU_COLOR_BREAK) ? "Couleur PAUSE" : "Couleur CONFIG";
      display.setCursor(0, 0);
      display.print(title);
      display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      int startIdx = max(0, menuIndex - 2);
      for (int i = 0; i < 5 && (startIdx + i) < PRESET_COLOR_COUNT; i++) {
        int idx = startIdx + i;
        display.setCursor(4, 12 + i * 10);
        if (idx == menuIndex) display.print(">");
        display.setCursor(12, 12 + i * 10);
        display.print(presetColorNames[idx]);
      }
      break;
    }

    case MENU_FX: {
      display.setCursor(0, 0);
      display.print("Effet fin de timer");
      display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      for (int i = 0; i < FX_COUNT; i++) {
        display.setCursor(4, 12 + i * 10);
        if (i == menuIndex) display.print(">");
        display.setCursor(12, 12 + i * 10);
        display.print(fxNames[i]);
      }
      break;
    }

    case MENU_BRIGHT: {
      display.setCursor(0, 0);
      display.print("Luminosite");
      display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
      display.setCursor(0, 20);
      display.print("Valeur : ");
      display.print((int)((cfg.brightness / 255.0f) * 100));
      display.print("%");
      int barW = (int)((cfg.brightness / 255.0f) * 116);
      display.drawRect(6, 36, 116, 10, SSD1306_WHITE);
      display.fillRect(6, 36, barW, 10, SSD1306_WHITE);
      display.setCursor(0, 52);
      display.print("Tourner = ajuster");
      break;
    }

    case MENU_FONT: {
      // Titre en police builtin
      display.setFont(nullptr);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Police : ");
      display.print(fontNames[cfg.fontIndex]);
      display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

      // Preview : afficher "12:34" avec la police sélectionnée, centré
      const GFXfont* prevFont = getFont(cfg.fontIndex);
      display.setFont(prevFont);
      display.setTextSize(fontTextSize[cfg.fontIndex]);

      String preview = "12:34";
      int16_t bx, by; uint16_t bw, bh;
      display.getTextBounds(preview, 0, 0, &bx, &by, &bw, &bh);
      int px = (SCREEN_WIDTH  - bw) / 2 - bx;
      int py = 12 + (50 - bh) / 2 - by; // zone y 12→62
      display.setCursor(px, py);
      display.print(preview);

      // Revenir builtin pour l'instruction bas
      display.setFont(nullptr);
      display.setTextSize(1);
      display.setCursor(2, 56);
      display.print("< tourner  valider >");
      break;
    }

    default: break;
  }

  display.display();
}

// Preview couleur sur tout le ruban (pour sélection menu)
void previewColorOnStrip(uint32_t color)
{
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >>  8) & 0xFF;
  uint8_t b =  color        & 0xFF;
  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, strip.Color(r, g, b));
  strip.show();
}

// ======================
// LED
// ======================

// Appelée depuis updateTimer() lors d'un tick seconde (met aussi à jour ledsOn)
void updateLEDs()
{
  if (mode == MODE_DONE || mode == MODE_CREDITS) return;

  uint32_t baseColor;
  if      (mode == MODE_SET)   baseColor = cfg.colorSet;
  else if (mode == MODE_WORK)  baseColor = cfg.colorWork;
  else                          baseColor = cfg.colorBreak;

  int ledsOn;
  if      (mode == MODE_WORK)  ledsOn = map(remainingSeconds, 0, workMinutes * 60, 0, LED_COUNT);
  else if (mode == MODE_BREAK) ledsOn = map(remainingSeconds, 0, BREAK_MINUTES * 60, 0, LED_COUNT);
  else                          ledsOn = LED_COUNT;

  float pulse = getPulseFactor();
  uint8_t r = ((baseColor >> 16) & 0xFF) * pulse;
  uint8_t g = ((baseColor >>  8) & 0xFF) * pulse;
  uint8_t b = ( baseColor        & 0xFF) * pulse;

  for (int i = 0; i < LED_COUNT; i++) {
    if (i < ledsOn) strip.setPixelColor(i, strip.Color(r, g, b));
    else            strip.setPixelColor(i, 0);
  }
  strip.show();
}

// Appelée en continu dans le loop() pour animer le pulse entre les ticks
// N'utilise pas remainingSeconds (ne change pas), juste le facteur de pulse
void updateLEDsContinuous()
{
  if (mode == MODE_DONE || mode == MODE_CREDITS || mode == MODE_SET) return;

  uint32_t baseColor = (mode == MODE_WORK) ? cfg.colorWork : cfg.colorBreak;

  int ledsOn;
  if (mode == MODE_WORK)       ledsOn = map(remainingSeconds, 0, workMinutes * 60, 0, LED_COUNT);
  else                          ledsOn = map(remainingSeconds, 0, BREAK_MINUTES * 60, 0, LED_COUNT);

  float pulse = getPulseFactor();
  uint8_t r = ((baseColor >> 16) & 0xFF) * pulse;
  uint8_t g = ((baseColor >>  8) & 0xFF) * pulse;
  uint8_t b = ( baseColor        & 0xFF) * pulse;

  for (int i = 0; i < LED_COUNT; i++) {
    if (i < ledsOn) strip.setPixelColor(i, strip.Color(r, g, b));
    else            strip.setPixelColor(i, 0);
  }
  strip.show();
}

// ======================
// EFFETS FIN DE TIMER
// ======================
uint32_t wheel(uint8_t pos)
{
  pos = 255 - pos;
  if (pos < 85)  return strip.Color(255 - pos * 3, 0, pos * 3);
  if (pos < 170) { pos -= 85;  return strip.Color(0, pos * 3, 255 - pos * 3); }
  pos -= 170;
  return strip.Color(pos * 3, 255 - pos * 3, 0);
}

// Moteur d'effet générique (appelé par runEndEffect et previewFxOnStrip)
void runEndEffectAt(uint8_t fx, unsigned long age)
{
  switch (fx) {
    case FX_RAINBOW: {
      uint8_t offset = (uint8_t)(age / 10);
      for (int i = 0; i < LED_COUNT; i++)
        strip.setPixelColor(i, wheel((i * 256 / LED_COUNT + offset) & 0xFF));
      strip.show();
      break;
    }
    case FX_FLASH: {
      bool on = (age / 250) % 2 == 0;
      uint32_t col = on ? cfg.colorWork : 0;
      for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, col);
      strip.show();
      break;
    }
    case FX_COMET: {
      int cometLen = 4;
      int head = (age / 40) % (LED_COUNT + cometLen);
      strip.clear();
      for (int i = 0; i < cometLen; i++) {
        int idx = head - i;
        if (idx >= 0 && idx < LED_COUNT) {
          float fade = 1.0f - (float)i / cometLen;
          uint8_t r = ((cfg.colorWork >> 16) & 0xFF) * fade;
          uint8_t g = ((cfg.colorWork >>  8) & 0xFF) * fade;
          uint8_t b = ( cfg.colorWork        & 0xFF) * fade;
          strip.setPixelColor(idx, strip.Color(r, g, b));
        }
      }
      strip.show();
      break;
    }
    case FX_BREATHE: {
      float phase = (float)(age % 2000) / 2000.0f;
      float bri   = (sin(phase * TWO_PI - PI / 2.0f) + 1.0f) / 2.0f;
      uint8_t r = ((cfg.colorWork >> 16) & 0xFF) * bri;
      uint8_t g = ((cfg.colorWork >>  8) & 0xFF) * bri;
      uint8_t b = ( cfg.colorWork        & 0xFF) * bri;
      for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(r, g, b));
      strip.show();
      break;
    }
    default:
      for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
      strip.show();
      break;
  }
}

void runEndEffect()
{
  runEndEffectAt(cfg.fxEnd, millis() - doneStartMs);
}

// Preview d'un effet pendant la navigation dans le menu FX
void previewFxOnStrip(uint8_t fx, unsigned long t)
{
  runEndEffectAt(fx, t);
}

// ======================
// FORMAT TEMPS
// ======================
String formatDuration(int seconds)
{
  if (seconds < 0) seconds = 0;
  int hours   = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs    = seconds % 60;
  char buf[16];
  if (hours > 0) sprintf(buf, "%dh%02d", hours, minutes);
  else           sprintf(buf, "%02d:%02d", minutes, secs);
  return String(buf);
}

// ======================
// HELPERS COULEUR WEB
// ======================
String colorToHex(uint32_t c)
{
  char buf[8];
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >>  8) & 0xFF;
  uint8_t b =  c        & 0xFF;
  sprintf(buf, "#%02x%02x%02x", r, g, b);
  return String(buf);
}

uint32_t hexToColor(String hex)
{
  hex.replace("#", "");
  long val = strtol(hex.c_str(), nullptr, 16);
  uint8_t r = (val >> 16) & 0xFF;
  uint8_t g = (val >>  8) & 0xFF;
  uint8_t b =  val        & 0xFF;
  return Adafruit_NeoPixel::Color(r, g, b);
}

// ======================
// BOOT SCREEN
// ======================
void showBootScreen()
{
  unsigned long start = millis();
  unsigned long dur   = 3000; // durée totale du boot screen

  // Centrage du bitmap sur l'écran 128x64
  int imgX = (SCREEN_WIDTH  - BOOT_IMG_W) / 2; // = 33
  int imgY = 2;                                  // haut de l'écran

  while (millis() - start < dur) {
    unsigned long age      = millis() - start;
    float         progress = (float)age / dur;

    // --- LEDs : sweep arc-en-ciel gauche → droite ---
    int litCount   = (int)(progress * LED_COUNT);
    uint8_t offset = (uint8_t)(age / 8);
    for (int i = 0; i < LED_COUNT; i++) {
      if (i <= litCount)
        strip.setPixelColor(i, wheel((i * 256 / LED_COUNT + offset) & 0xFF));
      else
        strip.setPixelColor(i, 0);
    }
    strip.show();

    // --- OLED ---
    display.clearDisplay();

    // Bitmap centré (apparaît dès le début)
    display.drawBitmap(imgX, imgY, bootBitmap, BOOT_IMG_W, BOOT_IMG_H, SSD1306_WHITE);

    // Ligne séparatrice sous le bitmap
    if (age > 300) {
      display.drawLine(0, imgY + BOOT_IMG_H + 2, 127, imgY + BOOT_IMG_H + 2, SSD1306_WHITE);
    }

    // "TIMER" sous le bitmap
    if (age > 500) {
      display.setTextSize(1);
      display.setCursor(48, imgY + BOOT_IMG_H + 6);
      display.print("TIMER");
    }

    // Barre de chargement en bas
    if (age > 700) {
      int barLen = (int)(((float)(age - 700) / (dur - 700)) * 110);
      barLen = constrain(barLen, 0, 110);
      display.drawRect(9, 54, 110, 7, SSD1306_WHITE);
      display.fillRect(9, 54, barLen, 7, SSD1306_WHITE);
    }

    // Version
    if (age > 1000) {
      display.setTextSize(1);
      display.setCursor(98, imgY + BOOT_IMG_H + 6); // même niveau que "TIMER"
      display.print("v3");
    }

    display.display();
    delay(16);
  }

  // Flash final LEDs blanches
  for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(255, 255, 255));
  strip.show();
  delay(150);
  strip.clear();
  strip.show();
}

// ======================
// EASTER EGG - CREDITS
// ======================
void initStars()
{
  randomSeed(analogRead(0));
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x     = random(0, SCREEN_WIDTH);
    stars[i].y     = random(0, SCREEN_HEIGHT);
    stars[i].speed = random(1, 3);
  }
}

void updateStars()
{
  if (millis() - lastStarUpdate < 60) return; // ~16fps pour les étoiles
  lastStarUpdate = millis();
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x -= stars[i].speed;
    if (stars[i].x < 0) {
      stars[i].x     = SCREEN_WIDTH - 1;
      stars[i].y     = random(0, SCREEN_HEIGHT);
      stars[i].speed = random(1, 3);
    }
  }
}

void showCreditsScreen()
{
  if (mode != MODE_CREDITS) return;

  updateStars();

  display.clearDisplay();

  // Etoiles en arrière-plan
  for (int i = 0; i < STAR_COUNT; i++) {
    // Les étoiles rapides sont plus grosses (2x2)
    if (stars[i].speed > 1)
      display.fillRect(stars[i].x, stars[i].y, 2, 2, SSD1306_WHITE);
    else
      display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
  }

  // Cadre
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  // Titre
  display.setTextSize(1);
  display.setCursor(28, 4);
  display.print("* CREDITS *");

  display.drawLine(1, 14, 126, 14, SSD1306_WHITE);

  // Crédits avec effet de défilement vertical basé sur le temps
  unsigned long age     = millis() - creditsStartMs;
  int scrollOffset      = (int)(age / 80) % 60; // défilement lent

  // Bloc de texte scrollable
  const char* lines[] = {
    "",
    "Code original",
    "  WILLAU",
    "",
    "  Update",
    "    &",
    "new features",
    "  _n3o_",
    "",
    "Made with <3",
  };
  const int LINE_COUNT = 10;

  int y0 = 18 - scrollOffset;
  for (int i = 0; i < LINE_COUNT; i++) {
    int yy = y0 + i * 10;
    if (yy >= 16 && yy < SCREEN_HEIGHT - 2) {
      display.setCursor(10, yy);
      display.setTextSize(1);
      display.print(lines[i]);
    }
  }

  // Instruction sortie en bas
  display.drawLine(1, 54, 126, 54, SSD1306_WHITE);
  display.setCursor(10, 56);
  display.setTextSize(1);
  display.print("Appuie pour quitter");

  display.display();
}

bool checkEasterEgg()
{
  unsigned long now = millis();

  // Timeout de la fenetre : reset
  if (eggClickCount > 0 && (now - eggFirstClick) > EGG_WINDOW_MS) {
    Serial.print("[EGG] Timeout reset, etait a ");
    Serial.println(eggClickCount);
    eggClickCount = 0;
  }

  if (eggClickCount == 0) eggFirstClick = now;

  eggClickCount++;
  Serial.print("[EGG] Clic ");
  Serial.print(eggClickCount);
  Serial.print("/");
  Serial.print(EGG_CLICKS);
  Serial.print(" (arcade:");
  Serial.print(EGG2_CLICKS);
  Serial.println(")");

  // Easter egg 2 : 6 clics → Arcade (priorité absolue)
  if (eggClickCount >= EGG2_CLICKS) {
    Serial.println("[EGG2] ARCADE DECLENCHE !");
    eggClickCount = 0;
    enterArcade();
    return true;
  }

  // Easter egg 1 : exactement 4 clics → attendre 500ms pour voir si un 5e arrive
  // Si oui on laisse le compteur continuer vers 6, sinon on déclenche les crédits
  if (eggClickCount == EGG_CLICKS) {
    Serial.println("[EGG1] 4 clics - attente 500ms...");
    unsigned long waitStart = millis();
    while (millis() - waitStart < 500) {
      server.handleClient(); // ne pas bloquer le serveur web
      // Si un nouveau clic arrive pendant l'attente, on continue
      if (digitalRead(BTN) == LOW) {
        // Debounce
        delay(30);
        if (digitalRead(BTN) == LOW) {
          // Attendre relâchement
          while (digitalRead(BTN) == LOW) delay(5);
          eggClickCount++;
          Serial.print("[EGG] Clic bonus: ");
          Serial.println(eggClickCount);
          if (eggClickCount >= EGG2_CLICKS) {
            Serial.println("[EGG2] ARCADE DECLENCHE !");
            eggClickCount = 0;
            enterArcade();
            return true;
          }
        }
      }
    }
    // Aucun clic supplémentaire → crédits
    Serial.println("[EGG1] CREDITS DECLENCHE !");
    eggClickCount  = 0;
    creditsStartMs = millis();
    initStars();
    mode           = MODE_CREDITS;
    return true;
  }
  return false;
}



// ======================
// MENU BOOT — PLEIN ECRAN
// ======================

void drawIconTomate(int cx, int cy)
{
  // Corps de la tomate (cercle principal)
  display.fillCircle(cx, cy + 2, 13, SSD1306_WHITE);
  // Reflet (petit cercle noir en haut gauche)
  display.fillCircle(cx - 4, cy - 4, 3, SSD1306_BLACK);
  // Tige (ligne verticale)
  display.drawLine(cx, cy - 11, cx, cy - 15, SSD1306_WHITE);
  // Feuilles (deux petits traits obliques)
  display.drawLine(cx, cy - 13, cx - 5, cy - 17, SSD1306_WHITE);
  display.drawLine(cx, cy - 13, cx + 5, cy - 17, SSD1306_WHITE);
  display.drawLine(cx, cy - 13, cx - 3, cy - 18, SSD1306_WHITE);
  display.drawLine(cx, cy - 13, cx + 3, cy - 18, SSD1306_WHITE);
}

void drawIconSablier(int cx, int cy)
{
  // Cadre haut et bas
  display.drawLine(cx - 10, cy - 14, cx + 10, cy - 14, SSD1306_WHITE);
  display.drawLine(cx - 10, cy + 14, cx + 10, cy + 14, SSD1306_WHITE);
  // Contour sablier (deux triangles)
  display.drawLine(cx - 10, cy - 14, cx,      cy,      SSD1306_WHITE);
  display.drawLine(cx + 10, cy - 14, cx,      cy,      SSD1306_WHITE);
  display.drawLine(cx - 10, cy + 14, cx,      cy,      SSD1306_WHITE);
  display.drawLine(cx + 10, cy + 14, cx,      cy,      SSD1306_WHITE);
  // Sable en haut (triangle plein partiel)
  for (int i = 0; i < 5; i++) {
    int w = 9 - i;
    display.drawLine(cx - w, cy - 14 + i, cx + w, cy - 14 + i, SSD1306_WHITE);
  }
  // Sable en bas (petit triangle plein)
  for (int i = 0; i < 4; i++) {
    int w = i;
    display.drawLine(cx - w, cy + 10 + i, cx + w, cy + 10 + i, SSD1306_WHITE);
  }
  // Filet de sable (point central)
  display.drawPixel(cx, cy - 1, SSD1306_WHITE);
  display.drawPixel(cx, cy,     SSD1306_WHITE);
  display.drawPixel(cx, cy + 1, SSD1306_WHITE);
}

void drawIconChrono(int cx, int cy)
{
  // Corps du chronomètre (cercle)
  display.drawCircle(cx, cy + 2, 12, SSD1306_WHITE);
  // Couronne (rectangle arrondi en haut)
  display.fillRect(cx - 3, cy - 12, 6, 3, SSD1306_WHITE);
  // Bouton poussoir (petit rect à droite)
  display.fillRect(cx + 3, cy - 14, 4, 2, SSD1306_WHITE);
  // Aiguille des secondes (vers 2h)
  display.drawLine(cx, cy + 2, cx + 7, cy - 4, SSD1306_WHITE);
  // Centre
  display.fillCircle(cx, cy + 2, 1, SSD1306_WHITE);
  // Graduations (4 petits traits)
  display.drawLine(cx,      cy - 10, cx,      cy - 8,  SSD1306_WHITE); // 12h
  display.drawLine(cx + 10, cy + 2,  cx + 8,  cy + 2,  SSD1306_WHITE); // 3h
  display.drawLine(cx,      cy + 14, cx,      cy + 12, SSD1306_WHITE); // 6h
  display.drawLine(cx - 10, cy + 2,  cx - 8,  cy + 2,  SSD1306_WHITE); // 9h
}

void drawIconAmpoule(int cx, int cy)
{
  // Corps de l'ampoule (cercle du haut)
  display.drawCircle(cx, cy - 2, 10, SSD1306_WHITE);
  // Remplissage partiel (intérieur)
  display.fillCircle(cx, cy - 2, 7, SSD1306_WHITE);
  // Culot (rectangles en bas)
  display.fillRect(cx - 5, cy + 8,  10, 3, SSD1306_BLACK); // espace
  display.drawRect(cx - 5, cy + 8,  10, 3, SSD1306_WHITE);
  display.drawRect(cx - 5, cy + 11, 10, 3, SSD1306_WHITE);
  // Jonction corps/culot
  display.drawLine(cx - 5, cy + 8, cx - 5, cy + 7, SSD1306_WHITE);
  display.drawLine(cx + 5, cy + 8, cx + 5, cy + 7, SSD1306_WHITE);
  // Rayons (4 lignes courtes autour)
  display.drawLine(cx,      cy - 14, cx,      cy - 13, SSD1306_WHITE);
  display.drawLine(cx + 12, cy - 6,  cx + 11, cy - 5,  SSD1306_WHITE);
  display.drawLine(cx - 12, cy - 6,  cx - 11, cy - 5,  SSD1306_WHITE);
  display.drawLine(cx + 10, cy + 6,  cx + 9,  cy + 5,  SSD1306_WHITE);
  display.drawLine(cx - 10, cy + 6,  cx - 9,  cy + 5,  SSD1306_WHITE);
}

void drawBootMenu()
{
  display.clearDisplay();
  display.setFont(nullptr);

  const char* labels[] = { "POMODORO", "TIMER", "CHRONO", "AMBIANCE" };

  // Icône en haut centrée
  int icx = 64;
  int icy = 18;

  switch (bootMenuIndex) {
    case 0: drawIconTomate(icx, icy);  break;
    case 1: drawIconSablier(icx, icy); break;
    case 2: drawIconChrono(icx, icy);  break;
    case 3: drawIconAmpoule(icx, icy); break;
  }

  // Nom du mode en taille 1, centré en milieu bas
  display.setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(labels[bootMenuIndex], 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2 - x1, 40);
  display.print(labels[bootMenuIndex]);

  // Points de navigation en bas
  int dotY      = 56;
  int dotSpacing = 10;
  int startX    = (SCREEN_WIDTH - (BOOT_MENU_COUNT * dotSpacing)) / 2 + 4;
  for (int i = 0; i < BOOT_MENU_COUNT; i++) {
    int dx = startX + i * dotSpacing;
    if (i == bootMenuIndex)
      display.fillCircle(dx, dotY, 3, SSD1306_WHITE);
    else
      display.drawCircle(dx, dotY, 3, SSD1306_WHITE);
  }

  display.display();

  // LEDs : couleur thématique par mode
  uint32_t colors[] = {
    strip.Color(255, 20, 0),    // Pomodoro : rouge tomate
    strip.Color(0, 150, 255),   // Timer : bleu
    strip.Color(0, 255, 150),   // Chrono : vert cyan
    strip.Color(180, 100, 255), // Ambiance : violet
  };
  // Respiration douce sur les LEDs
  float bri = (sin(millis() / 600.0f) + 1.0f) / 2.0f * 0.6f + 0.2f;
  uint32_t c = colors[bootMenuIndex];
  uint8_t r = ((c >> 16) & 0xFF) * bri;
  uint8_t g = ((c >>  8) & 0xFF) * bri;
  uint8_t b = ( c        & 0xFF) * bri;
  for (int i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, strip.Color(r, g, b));
  strip.show();
}

// ======================
// MINUTEUR SIMPLE
// ======================
void drawTimerSimple()
{
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(30, 0);
  display.print("MINUTEUR");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Temps restant en grand
  const GFXfont* fnt = getFont(cfg.fontIndex);
  display.setFont(fnt);
  display.setTextSize(fontTextSize[cfg.fontIndex]);
  String t = formatDuration(timerSimpleRemaining);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2 - x1, (SCREEN_HEIGHT - h) / 2 - y1 + 4);
  display.print(t);

  display.setFont(nullptr);
  display.setTextSize(1);
  if (!timerSimpleRunning) {
    display.setCursor(0, 56);
    display.print("Turn=regler Btn=start");
  } else {
    display.setCursor(20, 56);
    display.print("Btn=pause/stop");
  }
  display.display();
}

void updateTimerSimple()
{
  // Encodeur : régler la durée si pas en cours
  static bool lastBtnTS = HIGH;
  bool btnNow = digitalRead(BTN);

  long pos   = -encoder.read() / 4;
  int  delta = (int)(pos - lastEncoderPos);
  if (delta != 0) {
    lastEncoderPos = pos;
    if (!timerSimpleRunning) {
      timerSimpleMinutes = constrain(timerSimpleMinutes + delta, 1, 720);
      timerSimpleRemaining = timerSimpleMinutes * 60;
    }
    drawTimerSimple();
  }

  // Bouton court : start/pause
  if (lastBtnTS == HIGH && btnNow == LOW) {
    delay(30);
    if (digitalRead(BTN) == LOW) {
      btnPressTime = millis();
    }
  }
  if (lastBtnTS == LOW && btnNow == HIGH) {
    unsigned long dur = millis() - btnPressTime;
    if (dur < 800) {
      if (!timerSimpleRunning && timerSimpleRemaining > 0) {
        timerSimpleRunning = true;
        timerSimpleLast    = millis();
      } else {
        timerSimpleRunning = false;
      }
      drawTimerSimple();
    } else {
      // Long = retour menu boot
      timerSimpleRunning = false;
      mode = MODE_BOOT_MENU;
      encPosBootMenu = -encoder.read() / 4;
      drawBootMenu();
    }
  }
  lastBtnTS = btnNow;

  // Décompte
  if (timerSimpleRunning && millis() - timerSimpleLast >= 1000) {
    timerSimpleLast = millis();
    timerSimpleRemaining--;
    if (timerSimpleRemaining <= 0) {
      timerSimpleRemaining = 0;
      timerSimpleRunning   = false;
      // Fin : effet LEDs
      runEndEffectAt(cfg.fxEnd, 0);
      drawTimerSimple();
      delay(3000);
      timerSimpleRemaining = timerSimpleMinutes * 60;
    }
    drawTimerSimple();
  }

  // LEDs : barre de progression
  if (timerSimpleRunning) {
    int ledsOn = map(timerSimpleRemaining, 0, timerSimpleMinutes * 60, 0, LED_COUNT);
    float pulse = getPulseFactor();
    uint8_t r = ((cfg.colorWork >> 16) & 0xFF) * pulse;
    uint8_t g = ((cfg.colorWork >>  8) & 0xFF) * pulse;
    uint8_t b = ( cfg.colorWork        & 0xFF) * pulse;
    for (int i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, i < ledsOn ? strip.Color(r, g, b) : 0);
    strip.show();
  }
}

// ======================
// CHRONOMETRE
// ======================
void drawChrono()
{
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(35, 0);
  display.print("CHRONO");
  if (chronoRunning) {
    display.setCursor(95, 0);
    display.print(">>>");
  }
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Temps écoulé
  unsigned long elapsed = chronoRunning ?
    chronoElapsed + (millis() - chronoStart) : chronoElapsed;

  const GFXfont* fnt = getFont(cfg.fontIndex);
  display.setFont(fnt);
  display.setTextSize(fontTextSize[cfg.fontIndex]);
  String t = formatDuration((int)(elapsed / 1000));
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2 - x1, (SCREEN_HEIGHT - h) / 2 - y1 + 4);
  display.print(t);

  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Btn=start/stop Long=reset");
  display.display();
}

void updateChrono()
{
  static bool lastBtnChrono = HIGH;
  bool btnNow = digitalRead(BTN);

  // Mise à jour continue de l'affichage
  if (chronoRunning) drawChrono();

  // LEDs : arc-en-ciel rotatif pendant le chrono
  if (chronoRunning) {
    unsigned long age = chronoElapsed + (millis() - chronoStart);
    uint8_t offset = (uint8_t)(age / 20);
    for (int i = 0; i < LED_COUNT; i++)
      strip.setPixelColor(i, wheel((i * 256 / LED_COUNT + offset) & 0xFF));
    strip.show();
  }

  if (lastBtnChrono == HIGH && btnNow == LOW) {
    btnPressTime = millis();
  }
  if (lastBtnChrono == LOW && btnNow == HIGH) {
    unsigned long dur = millis() - btnPressTime;
    if (dur < 800) {
      if (!chronoRunning) {
        chronoStart   = millis();
        chronoRunning = true;
      } else {
        chronoElapsed += millis() - chronoStart;
        chronoRunning  = false;
        strip.clear(); strip.show();
      }
      drawChrono();
    } else {
      unsigned long holdStart = millis() - dur;
      if (dur > 1500) {
        // Très long = retour menu boot
        chronoRunning = false; chronoElapsed = 0;
        mode = MODE_BOOT_MENU;
        encPosBootMenu = -encoder.read() / 4;
        strip.clear(); strip.show();
        drawBootMenu();
      } else {
        // Long = reset
        chronoRunning = false;
        chronoElapsed = 0;
        strip.clear(); strip.show();
        drawChrono();
      }
    }
  }
  lastBtnChrono = btnNow;
}

// ======================
// MODE AMBIANCE
// ======================
void updateAmbiance()
{
  static bool lastBtnAmb = HIGH;
  bool btnNow = digitalRead(BTN);
  int  delta  = 0;

  // Encodeur : changer d'effet
  long pos = -encoder.read() / 4;
  delta    = (int)(pos - lastEncoderPos);
  if (delta != 0) {
    lastEncoderPos = pos;
    ambianceEffect = (ambianceEffect + delta + 3) % 3;
    display.clearDisplay();
    display.setFont(nullptr); display.setTextSize(1);
    const char* names[] = { "Respiration", "Arc-en-ciel", "Comete" };
    display.setCursor(20, 0); display.print("AMBIANCE");
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    display.setTextSize(2);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(names[ambianceEffect], 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2 - x1, 28);
    display.print(names[ambianceEffect]);
    display.setTextSize(1);
    display.setCursor(10, 56); display.print("Turn=effet Long=quitter");
    display.display();
  }

  // Animation LEDs
  unsigned long t = millis();
  switch (ambianceEffect) {
    case 0: { // Respiration lente
      float bri = (sin(t / 1500.0f) + 1.0f) / 2.0f;
      uint8_t r = ((cfg.colorWork >> 16) & 0xFF) * bri;
      uint8_t g = ((cfg.colorWork >>  8) & 0xFF) * bri;
      uint8_t b = ( cfg.colorWork        & 0xFF) * bri;
      for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(r, g, b));
      strip.show(); break;
    }
    case 1: { // Arc-en-ciel
      uint8_t offset = (uint8_t)(t / 15);
      for (int i = 0; i < LED_COUNT; i++)
        strip.setPixelColor(i, wheel((i * 256 / LED_COUNT + offset) & 0xFF));
      strip.show(); break;
    }
    case 2: { // Comète
      int cometLen = 4;
      int head = (t / 60) % (LED_COUNT + cometLen);
      strip.clear();
      for (int i = 0; i < cometLen; i++) {
        int idx = head - i;
        if (idx >= 0 && idx < LED_COUNT) {
          float fade = 1.0f - (float)i / cometLen;
          uint8_t r = ((cfg.colorWork >> 16) & 0xFF) * fade;
          uint8_t g = ((cfg.colorWork >>  8) & 0xFF) * fade;
          uint8_t bv= ( cfg.colorWork        & 0xFF) * fade;
          strip.setPixelColor(idx, strip.Color(r, g, bv));
        }
      }
      strip.show(); break;
    }
  }

  // Bouton long = retour menu boot
  if (lastBtnAmb == HIGH && btnNow == LOW) btnPressTime = millis();
  if (lastBtnAmb == LOW  && btnNow == HIGH) {
    if (millis() - btnPressTime > 800) {
      mode = MODE_BOOT_MENU;
      encPosBootMenu = -encoder.read() / 4;
      strip.clear(); strip.show();
      drawBootMenu();
    }
  }
  lastBtnAmb = btnNow;

  // Affichage initial
  static bool ambianceDrawn = false;
  if (!ambianceDrawn) {
    display.clearDisplay();
    display.setFont(nullptr); display.setTextSize(1);
    display.setCursor(20, 0); display.print("AMBIANCE");
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    display.setTextSize(2);
    const char* names[] = { "Respiration", "Arc-en-ciel", "Comete" };
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(names[ambianceEffect], 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2 - x1, 28);
    display.print(names[ambianceEffect]);
    display.setTextSize(1);
    display.setCursor(10, 56); display.print("Turn=effet Long=quitter");
    display.display();
    ambianceDrawn = true;
  }
}

void startWebServer()
{
  server.on("/",     handleWebRoot);
  server.on("/save", HTTP_POST, handleWebSave);
  server.begin();
}

void handleWebRoot()
{
  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>POMODOROX Timer Config</title>
<style>
  body { font-family: sans-serif; max-width: 480px; margin: 0 auto; padding: 16px; background: #111; color: #eee; }
  h1   { color: #fff; font-size: 1.4em; margin-bottom: 4px; }
  h2   { color: #aaa; font-size: 1em; margin: 16px 0 6px; }
  label { display: block; margin: 8px 0 2px; font-size: 0.9em; color: #bbb; }
  input[type=color]  { width: 60px; height: 36px; border: none; border-radius: 6px; cursor: pointer; background: none; }
  input[type=range]  { width: 100%; margin: 4px 0; }
  select { width: 100%; padding: 8px; border-radius: 6px; background: #222; color: #eee; border: 1px solid #444; }
  .row   { display: flex; align-items: center; gap: 12px; }
  .preview { width: 80px; height: 36px; border-radius: 6px; border: 1px solid #444; display: inline-block; vertical-align: middle; }
  button { width: 100%; padding: 12px; margin-top: 20px; background: #6200ea; color: #fff;
           border: none; border-radius: 8px; font-size: 1em; cursor: pointer; }
  button:hover { background: #7c4dff; }
  .success { background: #1b5e20; padding: 10px; border-radius: 6px; display: none; }
  .val-label { min-width: 40px; text-align: right; font-size: 0.85em; color: #aaa; }
</style>
</head>
<body>
<h1>&#9201; POMODOROX Timer Config</h1>

<form method="POST" action="/save" id="form">

<h2>Couleurs des états</h2>

<label>Mode WORK</label>
<div class="row">
  <input type="color" name="cWork" id="cWork" value=")rawhtml";

  html += colorToHex(cfg.colorWork);
  html += R"rawhtml(" oninput="upv('pvWork',this.value)">
  <div class="preview" id="pvWork" style="background:)rawhtml";
  html += colorToHex(cfg.colorWork);
  html += R"rawhtml("></div>
</div>

<label>Mode PAUSE</label>
<div class="row">
  <input type="color" name="cBreak" id="cBreak" value=")rawhtml";
  html += colorToHex(cfg.colorBreak);
  html += R"rawhtml(" oninput="upv('pvBreak',this.value)">
  <div class="preview" id="pvBreak" style="background:)rawhtml";
  html += colorToHex(cfg.colorBreak);
  html += R"rawhtml("></div>
</div>

<label>Mode CONFIG (Set)</label>
<div class="row">
  <input type="color" name="cSet" id="cSet" value=")rawhtml";
  html += colorToHex(cfg.colorSet);
  html += R"rawhtml(" oninput="upv('pvSet',this.value)">
  <div class="preview" id="pvSet" style="background:)rawhtml";
  html += colorToHex(cfg.colorSet);
  html += R"rawhtml("></div>
</div>

<h2>Effet fin de timer</h2>
<select name="fxEnd">
  <option value="0")rawhtml";
  html += (cfg.fxEnd == 0) ? " selected" : "";
  html += ">Aucun</option><option value=\"1\"";
  html += (cfg.fxEnd == 1) ? " selected" : "";
  html += ">Arc-en-ciel</option><option value=\"2\"";
  html += (cfg.fxEnd == 2) ? " selected" : "";
  html += ">Flash</option><option value=\"3\"";
  html += (cfg.fxEnd == 3) ? " selected" : "";
  html += ">Comète</option><option value=\"4\"";
  html += (cfg.fxEnd == 4) ? " selected" : "";
  html += R"rawhtml(">Respiration</option>
</select>

<h2>Luminosité LED</h2>
<div class="row">
  <input type="range" name="brightness" id="bri" min="10" max="255" value=")rawhtml";
  html += String(cfg.brightness);
  html += R"rawhtml(" oninput="document.getElementById('briVal').textContent=Math.round(this.value/255*100)+'%'">
  <span class="val-label" id="briVal">)rawhtml";
  html += String((int)(cfg.brightness / 255.0f * 100));
  html += R"rawhtml(%</span>
</div>

<h2>Pulse LED (secondes)</h2>
<label>Intensité minimale (fond)</label>
<div class="row">
  <input type="range" name="pulseMin" id="pmin" min="0" max="100" value=")rawhtml";
  html += String((int)(cfg.pulseMin * 100));
  html += R"rawhtml(" oninput="document.getElementById('pminV').textContent=this.value+'%'">
  <span class="val-label" id="pminV">)rawhtml";
  html += String((int)(cfg.pulseMin * 100));
  html += R"rawhtml(%</span>
</div>
<label>Intensité maximale</label>
<div class="row">
  <input type="range" name="pulseMax" id="pmax" min="0" max="100" value=")rawhtml";
  html += String((int)(cfg.pulseMax * 100));
  html += R"rawhtml(" oninput="document.getElementById('pmaxV').textContent=this.value+'%'">
  <span class="val-label" id="pmaxV">)rawhtml";
  html += String((int)(cfg.pulseMax * 100));
  html += R"rawhtml(%</span>
</div>

<h2>Police d'affichage</h2>
<select name="fontIndex" id="fontSel" onchange="updateFontPreview()">
  <option value="0">Builtin</option>
  <option value="1">FreeSans 9pt</option>
  <option value="2">FreeSansBold 9pt</option>
  <option value="3">FreeSansBold 12pt</option>
  <option value="4">FreeSansBoldOblique 12pt</option>
  <option value="5">FreeMono 9pt</option>
  <option value="6">FreeSerif 9pt</option>
  <option value="7">FreeSerifBold 12pt</option>
  <option value="8">FreeSerifBoldItalic 9pt</option>
  <option value="9">Org_01 (pixel)</option>
  <option value="10">TomThumb (micro)</option>
  <option value="11">Picopixel (micro)</option>
</select>

<div id="fontPreview" style="margin:12px 0;background:#000;border-radius:8px;padding:8px;text-align:center;border:1px solid #333;">
  <canvas id="oledCanvas" width="128" height="64" style="image-rendering:pixelated;width:256px;height:128px;display:block;margin:0 auto;"></canvas>
  <div style="font-size:11px;color:#666;margin-top:4px;">Preview OLED 128x64 (x2)</div>
</div>

<button type="submit">&#128190; Enregistrer</button>
</form>

<div class="success" id="ok">&#10003; Paramètres sauvegardés !</div>

<script>
function upv(id, val) { document.getElementById(id).style.background = val; }
const params = new URLSearchParams(location.search);
if (params.get('saved') === '1') {
  const d = document.getElementById('ok');
  d.style.display = 'block';
  setTimeout(() => d.style.display = 'none', 3000);
}

// ── Police & preview OLED ──
const FONT_IDX = )rawhtml";
  html += String(cfg.fontIndex);
  html += R"rawhtml(;
document.getElementById('fontSel').value = FONT_IDX;

const FONTS = [
  { name:'Builtin',    family:'monospace',      size:28, weight:'900' },
  { name:'FreeSans9',  family:'sans-serif',     size:14, weight:'400' },
  { name:'FreeSansBold9', family:'sans-serif',  size:14, weight:'700' },
  { name:'FreeSansBold12',family:'sans-serif',  size:18, weight:'700' },
  { name:'FreeSansBoldObl12',family:'sans-serif',size:18,weight:'700',style:'italic' },
  { name:'FreeMono9',  family:'monospace',      size:14, weight:'400' },
  { name:'FreeSerif9', family:'serif',          size:14, weight:'400' },
  { name:'FreeSerifBold12',family:'serif',      size:18, weight:'700' },
  { name:'FreeSerifBoldIt9',family:'serif',     size:14, weight:'700',style:'italic' },
  { name:'Org_01',     family:'"Courier New"',  size:9,  weight:'400' },
  { name:'TomThumb',   family:'monospace',      size:7,  weight:'400' },
  { name:'Picopixel',  family:'monospace',      size:6,  weight:'400' }
];

function updateFontPreview() {
  const idx = parseInt(document.getElementById('fontSel').value);
  const f   = FONTS[idx];
  const canvas = document.getElementById('oledCanvas');
  const ctx    = canvas.getContext('2d');
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, 128, 64);
  ctx.fillStyle = '#fff';
  ctx.font = (f.style||'normal') + ' ' + f.weight + ' ' + f.size + 'px ' + f.family;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText('12:34', 64, 38);
  // label mode
  ctx.font = '400 6px monospace';
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  ctx.fillText('WORK', 0, 0);
  // nom police
  ctx.fillStyle = '#555';
  ctx.font = '400 5px monospace';
  ctx.textAlign = 'center';
  ctx.fillText(f.name, 64, 58);
}
updateFontPreview();
</script>
</body>
</html>
)rawhtml";

  server.send(200, "text/html", html);
}

void handleWebSave()
{
  if (server.hasArg("cWork"))      cfg.colorWork  = hexToColor(server.arg("cWork"));
  if (server.hasArg("cBreak"))     cfg.colorBreak = hexToColor(server.arg("cBreak"));
  if (server.hasArg("cSet"))       cfg.colorSet   = hexToColor(server.arg("cSet"));
  if (server.hasArg("fxEnd"))      cfg.fxEnd      = server.arg("fxEnd").toInt();
  if (server.hasArg("brightness")) cfg.brightness = constrain(server.arg("brightness").toInt(), 10, 255);
  if (server.hasArg("pulseMin"))   cfg.pulseMin   = constrain(server.arg("pulseMin").toInt(), 0, 100) / 100.0f;
  if (server.hasArg("pulseMax"))   cfg.pulseMax   = constrain(server.arg("pulseMax").toInt(), 0, 100) / 100.0f;
  if (server.hasArg("fontIndex"))  cfg.fontIndex  = constrain(server.arg("fontIndex").toInt(), 0, FONT_COUNT - 1);

  saveConfig();
  strip.setBrightness(cfg.brightness);
  updateLEDs();

  server.sendHeader("Location", "/?saved=1");
  server.send(302);
}
