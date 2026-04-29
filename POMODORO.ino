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
  1.0f                                      // pulseMax
};

// =====================
// MODES PRINCIPAUX
// =====================
enum Mode { MODE_SET, MODE_WORK, MODE_BREAK, MODE_DONE, MODE_CREDITS };
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
#define EGG_WINDOW_MS   2000   // fenêtre de détection (ms)
#define CREDITS_DURATION 8000  // durée affichage crédits (ms)

int   eggClickCount    = 0;
unsigned long eggFirstClick = 0;
unsigned long creditsStartMs = 0;

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
  MENU_BRIGHT       // luminosité
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
  "Web portal",
  "Retour"
};
const int MAIN_MENU_COUNT = 7;

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
// ENCODEUR / BOUTON
// =====================
long lastEncoderPos  = 0;
bool lastButtonState = HIGH;
unsigned long btnPressTime = 0;
bool btnLongHandled = false;

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
  strip.setBrightness(cfg.brightness);

  initStars();
  showBootScreen();

  // Démarrage WiFi AP + serveur web
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  startWebServer();

  drawScreen();
  updateLEDs();
}

// ======================
// LOOP
// ======================
void loop()
{
  server.handleClient();
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
        // L'easter egg vient de s'ouvrir, on ne fait rien d'autre
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

  display.clearDisplay();

  if (mode == MODE_DONE) {
    display.setTextSize(2);
    display.setCursor(20, 20);
    display.print("TERMINE !");
    display.display();
    return;
  }

  String timeStr = formatDuration(remainingSeconds);
  display.setTextSize(4);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH  - w) / 2;
  int y = (SCREEN_HEIGHT - h) / 2;

  display.setCursor(x, y);
  display.print(timeStr);

  // Indicateur de mode en haut à gauche (petit)
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
      // Preview couleur selon mode courant
      previewColorOnStrip(mode == MODE_WORK ? cfg.colorWork :
                          mode == MODE_BREAK ? cfg.colorBreak : cfg.colorSet);
      drawMenu();
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
        case 5:
          // Afficher infos portail web
          display.clearDisplay();
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
        case 6: exitMenu(); break;
      }
      break;

    case MENU_COLOR_WORK:
      cfg.colorWork = presetColors[menuIndex];
      enterMenu(MENU_MAIN);
      break;
    case MENU_COLOR_BREAK:
      cfg.colorBreak = presetColors[menuIndex];
      enterMenu(MENU_MAIN);
      break;
    case MENU_COLOR_SET:
      cfg.colorSet = presetColors[menuIndex];
      enterMenu(MENU_MAIN);
      break;

    case MENU_FX:
      cfg.fxEnd = menuIndex;
      enterMenu(MENU_MAIN);
      break;

    case MENU_BRIGHT:
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
      // Barre de progression
      int barW = (int)((cfg.brightness / 255.0f) * 116);
      display.drawRect(6, 36, 116, 10, SSD1306_WHITE);
      display.fillRect(6, 36, barW, 10, SSD1306_WHITE);
      display.setCursor(0, 52);
      display.print("Tourner = ajuster");
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
      display.setCursor(98, 56);
      // Inversion locale pour la version (texte noir sur barre blanche si chevauchement)
      display.print("v2.0");
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
    "Update & design",
    "  _n3o_",
    "",
    "Built with",
    "  ESP32-C3",
    "  NeoPixel",
    "  SSD1306",
    "",
    "Made with <3",
  };
  const int LINE_COUNT = 13;

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
  Serial.println(EGG_CLICKS);

  if (eggClickCount >= EGG_CLICKS) {
    Serial.println("[EGG] DECLENCHE !");
    eggClickCount  = 0;
    creditsStartMs = millis();
    initStars();
    mode           = MODE_CREDITS;
    return true;
  }
  return false;
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
<title>Timer Config</title>
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
<h1>&#9201; Timer Config</h1>

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

  saveConfig();
  strip.setBrightness(cfg.brightness);
  updateLEDs();

  server.sendHeader("Location", "/?saved=1");
  server.send(302);
}
