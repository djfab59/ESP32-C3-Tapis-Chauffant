#include <U8g2lib.h>
#include <Wire.h>
#include <Bounce2.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <RTClib.h>
#include <cstring>
#include "pins.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Broches I2C centralisées dans include/pins.h


// Wifi
static const char* ssid = WIFI_SSID;
static const char* password = WIFI_PASSWORD;

// Utilisation du constructeur SH1106 pour ton clone
//U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// RTC
RTC_DS3231 rtc;

// Capteur de température
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);

// Broches des boutons (depuis include/pins.h)
#define BTN_BAS     PIN_BTN_BAS
#define BTN_GAUCHE  PIN_BTN_GAUCHE
#define BTN_HAUT    PIN_BTN_HAUT
#define BTN_DROITE  PIN_BTN_DROITE

// Bounce2 pour chaque bouton
Bounce btnHaut   = Bounce();
Bounce btnBas    = Bounce();
Bounce btnGauche = Bounce();
Bounce btnDroite = Bounce();

// Variables de suivi pour les boutons
static unsigned long hautPressedSince = 0, hautLastRepeat = 0;
static unsigned long basPressedSince  = 0, basLastRepeat  = 0;

// Paramètres d'auto-repeat
const unsigned long delay1 = 500;   // après 500ms -> repeat lent
const unsigned long delay2 = 2500;   // après 2500ms -> repeat rapide
const unsigned long rate1  = 500;    // vitesse lente : 500ms
const unsigned long rate2  = 100;    // vitesse rapide : 100ms

// Variables d'état du menu
int menuIndex = 0;     // 0=Accueil, 1=Heure, 2=Ctrl Temp, 3=Version
bool inMenu = false;

// Variables température
float tempAct = 25.5;  // Température actuelle
float tempCible = 25.0; // Température à atteindre

void setup() {
  Serial.begin(9600);

  // Initialisation du capteur de température
  ds.begin();
  ds.setResolution(11); // 11 bits = 375 ms pour 0.125 °C de précision
  ds.setWaitForConversion(false);  // pas d’attente bloquante

  // Initialisation de l'écran
  Wire.begin(PIN_SDA, PIN_SCL);
  u8g2.begin();

  // Initialisation des boutons
  pinMode(BTN_HAUT,   INPUT_PULLUP);
  pinMode(BTN_BAS,    INPUT_PULLUP);
  pinMode(BTN_GAUCHE, INPUT_PULLUP);
  pinMode(BTN_DROITE, INPUT_PULLUP);

  // Initialisation des objets Bounce
  btnHaut.attach(BTN_HAUT);     btnHaut.interval(25);
  btnBas.attach(BTN_BAS);       btnBas.interval(25);
  btnGauche.attach(BTN_GAUCHE); btnGauche.interval(25);
  btnDroite.attach(BTN_DROITE); btnDroite.interval(25);

  Serial.print("Coucou");
  // Initialisation du Wifi
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (std::strlen(ssid) > 0) {
    u8g2.drawStr(0, 8, "Wifi connecting...");
    WiFi.begin(ssid, password);
    int x = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      u8g2.drawStr(x, 16, ".");
      u8g2.sendBuffer();
      x = x + 8;
    }
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 24, "Wifi connected!!!");
  } else {
    u8g2.drawStr(0, 24, "Wifi disabled (no creds)");
  }
  u8g2.sendBuffer();
  delay(1000);

  // Initialisation du RTC
  if (!rtc.begin()) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 24, "RTC introuvable !");
    u8g2.sendBuffer();
    delay(1000);
    while (1);
  }
  if (rtc.lostPower()) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 24, "Le RTC a perdu l'heure.");
    u8g2.drawStr(0, 40, "Réglage nécessaire !!!");
    u8g2.sendBuffer();
    delay(1000);
    // Crée un objet DateTime avec l'heure de compilation
    DateTime compileTime(F(__DATE__), F(__TIME__));
    // Ajoute 50 secondes
    DateTime adjusted = compileTime + TimeSpan(0, 0, 0, 20);
    // Applique au RTC
    rtc.adjust(adjusted);
  }

  // Initialisation de l'OTA
  ArduinoOTA.begin();
}

// Fonction affichage menu
void drawMenu() {
  const char* items[] = {"Heure", "Ctl Temp", "Version"};
  
  for (int i = 0; i < 3; i++) {
    int y = 20 + i * 20; // position verticale
    
    if (i + 1 == menuIndex) {
      // rectangle de sélection
      u8g2.drawBox(0, y - 14, 128, 16); // -14 pour décaler le haut
      u8g2.setDrawColor(0); // texte noir
    } else {
      u8g2.setDrawColor(1); // texte blanc
    }
    
    // dessiner le texte
    u8g2.setFont(u8g2_font_fub14_tr); // choisir police adaptée
    u8g2.drawStr(2, y, items[i]);
    
    u8g2.setDrawColor(1); // remettre blanc pour la suite
  }
}

// Fonction générique d’auto-repeat
void handleRepeat(Bounce &btn, float &target, float step, unsigned long &pressedSince, unsigned long &lastRepeat) {
  unsigned long now = millis();

  if (btn.fell()) {
    // Action immédiate
    target += step;
    pressedSince = now;
    lastRepeat   = now;
  }

  if (btn.read() == LOW) { // Bouton maintenu
    unsigned long held = now - pressedSince;
    unsigned long interval = 0;

    if (held > delay2) {
      interval = rate2;    // très rapide
    } else if (held > delay1) {
      interval = rate1;    // rapide
    }

    if (interval > 0 && now - lastRepeat >= interval) {
      target += step;
      lastRepeat = now;
    }
  }
}

void drawWiFiArc(U8G2 &u8g2, int x, int y, int r, int startAngle, int endAngle) {
  for (int a = startAngle; a <= endAngle; a++) {
    float rad = a * 3.14159 / 180.0;
    int px = x + r * cos(rad);
    int py = y - r * sin(rad);
    u8g2.drawPixel(px, py);
  }
}

void drawWiFiIcon(U8G2 &u8g2, int x, int y, long rssi) {
  // nombre d'arcs selon le RSSI
  // 0 -> dead!
  // -80 à 85 : poor point
  // - 75 à 80 : moyen- un arc
  // - 70 à 75 : moyen + deux arc
  // - 1 à 70 : bon trois arc
  // point
  if (rssi != 0 && rssi > -86)
  {
    u8g2.drawPixel(x-1, y-1);
    u8g2.drawPixel(x,   y-1);
    u8g2.drawPixel(x-2, y);
    u8g2.drawPixel(x-1, y);
    u8g2.drawPixel(x,   y);
    u8g2.drawPixel(x+1, y);
    u8g2.drawPixel(x-1, y+1);
    u8g2.drawPixel(x,   y+1);
  }
  // un arc
  if (rssi != 0 && rssi > -79)
  {
    drawWiFiArc(u8g2, x, y, 4, 40, 140);
  }
  // deux arcs
  if (rssi != 0 && rssi > -74)
  {
    drawWiFiArc(u8g2, x, y, 7, 40, 140);
  }
  // trois arcs
  if (rssi != 0 && rssi > -64)
  {
    drawWiFiArc(u8g2, x, y, 10, 40, 140);
  }
}

void loop() {
  Serial.print("Dans le loop");

  // Activation de l'OTA
  ArduinoOTA.handle();

  // Récupération de la date et de l'heure
  DateTime now = rtc.now();
  char date[30];
  //sprintf(date, "%02d/%02d/%04d %02d:%02d:%02d",
  //  now.day(), now.month(), now.year(),
  //  now.hour(), now.minute(), now.second());
  sprintf(date, "%02d/%02d %02d:%02d:%02d",
    now.day(), now.month(), now.hour(), now.minute(), now.second());

  // Mise à jour de la tempéraure
  // Timer pour la température
  static unsigned long lastRequest = 0;
  if (millis() - lastRequest > 1000) {  // toutes les 1s
    ds.requestTemperatures(); 
    lastRequest = millis();
  }
  float t = ds.getTempCByIndex(0);   // on lit la dernière valeur dispo (sans bloquer)
  if (t == DEVICE_DISCONNECTED_C) {
    tempAct=66.6;
  } else {
    tempAct=t;
  }

  // Récupération de la puissance du signal WiFi
  // Timer pour le RSSI
  static unsigned long lastRSSIRequest = 0;
  static long rssi = 0;
  if (millis() - lastRSSIRequest > 1000) {  // toutes les 1s
    rssi = WiFi.RSSI();
    lastRSSIRequest = millis();
  }

  // Mise à jour debounce
  btnHaut.update();
  btnBas.update();
  btnGauche.update();
  btnDroite.update();

  // Navigation menu
  if (btnDroite.fell()) {
    inMenu = true;
    if (menuIndex == 0) menuIndex = 1;
  }
  if (inMenu) {
    if (btnHaut.fell() && menuIndex > 1)   menuIndex--;
    if (btnBas.fell()  && menuIndex < 3)   menuIndex++;
    if (btnGauche.fell()) {
      inMenu = false;
      menuIndex = 0;
    }
  } else {
    // Ajustement température quand on est en Accueil
    //if (btnHaut.fell()) tempCible += 0.1;
    //if (btnBas.fell())  tempCible -= 0.1;
    handleRepeat(btnHaut, tempCible, +0.1, hautPressedSince, hautLastRepeat);
    handleRepeat(btnBas,  tempCible, -0.1, basPressedSince,  basLastRepeat);
  }


  u8g2.clearBuffer(); // efface le buffer
  if (!inMenu) {
    // Affichage de l'écran d'accueil

    // Affichage de l'heure
    u8g2.setFont(u8g2_font_ncenB08_tr); // Choix de la police
    u8g2.drawStr(0, 8, date);

    // Affichage de la température actuelle
    // Conversion de float en chaîne de caractères
    char tempStrAct[16];
    sprintf(tempStrAct, "%.1f", tempAct); // Convertit tempAct en chaîne de caractères avec 1 décimale
    u8g2.setFont(u8g2_font_fub25_tr);
    u8g2.drawStr(25, 45, tempStrAct); // Affiche la température
    u8g2.setFont(u8g2_font_fub11_tr);
    u8g2.drawStr(93, 27, "o");

    // Affichage de la température cible
    char tempStrCible[16];
    sprintf(tempStrCible, "%.1f", tempCible); // Convertit tempCible en chaîne de caractères avec 1 décimale
    u8g2.setFont(u8g2_font_t0_12_tf);
    u8g2.drawStr(95, 64, tempStrCible);
    u8g2.setFont(u8g2_font_tiny5_tf);
    u8g2.drawStr(120, 59, "o");

    if (tempAct<tempCible)
    {
      u8g2.setFont(u8g2_font_open_iconic_embedded_2x_t);
      u8g2.drawGlyph(0, 64, 0x0043);
    }

    // Affichage du signal wifi
    int x=120;
    int y=10;
    drawWiFiIcon(u8g2, x, y, rssi);
    //u8g2.setFont(u8g2_font_t0_12_tf);
    //u8g2.drawStr(11, 16, (String(rssi)).c_str());
  } else {
      drawMenu();
  }
  u8g2.sendBuffer(); // envoie à l'écran
  //delay(2000);
}
