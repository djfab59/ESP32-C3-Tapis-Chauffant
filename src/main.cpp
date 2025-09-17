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

//Broches + Screen centralisées dans include/pins.h
// Utilisation du constructeur SH1106 pour ton clone
//U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Wifi
static const char* ssid = WIFI_SSID;
static const char* password = WIFI_PASSWORD;

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
const unsigned long rate2  = 50;    // vitesse rapide : 100ms

// Affichage non bloquant du message "Saved !"
unsigned long saveMsgUntil = 0;                 // moment où on cesse l'affichage
const unsigned long saveMsgDuration = 2000;     // durée d'affichage en ms

// Variables d'état du menu
enum ScreenState {
  Accueil,
  Menu,
  Date,
  Temp,
  Wifi,
  Version
};
ScreenState menuState = Accueil;
// 0=Accueil, 1=Date, 2=Temp, 3=Wifi, 4=Version
int menuIndex = 0;

// Variables température
float tempAct = 25.5;  // Température actuelle
float tempCible = 25.0; // Température à atteindre

// Variables date
int day = 30, month = 12, year = 2025;
int hour = 13, minute = 45;

// Variable Prog Temp
int progHourDay = 9;
int progMinuteDay = 30;
float progTempDay = 25.5;
int progHourNight = 19;
int progMinuteNight = 0;
float progTempNight = 20.5;
int progHourDayTemp, progMinuteDayTemp, progHourNightTemp, progMinuteNightTemp;
float progTempDayTemp, progTempNightTemp;

void setup() {
  Serial.begin(9600);
  Serial.print("Setup!");

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

  // Initialisation du relais
  pinMode(PIN_RELAY, OUTPUT);

  // Initialisation du Wifi
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (std::strlen(ssid) > 0) {
    u8g2.drawStr(0, 8, "Wifi connecting...");
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();
    int x = 0;
    // Attendre au max 10 secondes
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      u8g2.drawStr(x, 16, ".");
      u8g2.sendBuffer();
      x = x + 6;
    }
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    if (WiFi.status() == WL_CONNECTED) {
      u8g2.drawStr(0, 24, "Wifi connected!!!");
    } else {
      u8g2.drawStr(0, 24, "Wifi failed (timeout)");
    }
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
  const char* items[] = {"Date", "Prog Temp", "Wifi", "Version"};
  int marge = 16;

  for (int i = 0; i < 4; i++) {
    int y = marge + i * marge; // position verticale

    if (i + 1 == menuIndex) {
      // rectangle de sélection
      u8g2.drawBox(0, y - marge, 128, marge); // -14 pour décaler le haut
      u8g2.setDrawColor(0); // texte noir
    } else {
      u8g2.setDrawColor(1); // texte blanc
    }

    // dessiner le texte
    u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
    u8g2.drawStr(2, y -4, items[i]);
    u8g2.setDrawColor(1); // remettre blanc pour la suite
  }
}

// Fonction affichage programation date
void drawDate() {
  // dessiner le texte
  u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
  u8g2.drawStr(30, 11, "DateProg");
  u8g2.setFont(u8g2_font_fub11_tr);
  //menuIndex
  // Tableau de correspondance des flèches
  int tableau[2][5] = {
    {22+9, 49+9, 76+9+9, 40+9, 66+9},
    {35-15, 35-15, 35-15, 57-15, 57-15}
  };

  // Flèche haut
  u8g2.drawPixel(tableau[0][menuIndex-1]-3, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]);
  u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]+3, tableau[1][menuIndex-1]);
  u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]-1);
  u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]-2);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]-2);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]-2);
  u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]-3);
  // Flèche bas
  u8g2.drawPixel(tableau[0][menuIndex-1]-3, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18);
  u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]+3, tableau[1][menuIndex-1]+18);
  u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]+18+1);
  u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]+18+2);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18+2);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]+18+2);
  u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18+3);

  String sday = (day < 10 ? "0" : "") + String(day);
  u8g2.drawStr(22, 35, sday.c_str());
  u8g2.drawStr(42, 35, "/");
  String smonth = (month < 10 ? "0" : "") + String(month);
  u8g2.drawStr(49, 35, smonth.c_str());
  u8g2.drawStr(69, 35, "/");
  String syear = String(year);
  u8g2.drawStr(76, 35, syear.c_str());

  String shour = (hour < 10 ? "0" : "") + String(hour);
  u8g2.drawStr(40, 57, shour.c_str());
  u8g2.drawStr(60, 57, ":");
  String sminute = (minute < 10 ? "0" : "") + String(minute);
  u8g2.drawStr(66, 57, sminute.c_str());
}

// Fonction affichage programation température
void drawTemp() {
  u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
  u8g2.drawStr(30, 11, "TempProg");

 // Tableau de correspondance des flèches
  int tableau[2][6] = {
    {24+9, 51+9, 88+9+9, 24+9, 51+9, 88+9+9},
    {35-15, 35-15, 35-15, 57-15, 57-15, 57-15}
  };

  // Flèche haut
  u8g2.drawPixel(tableau[0][menuIndex-1]-3, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]);
  u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]);u8g2.drawPixel(tableau[0][menuIndex-1]+3, tableau[1][menuIndex-1]);
  u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]-1);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]-1);
  u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]-2);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]-2);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]-2);
  u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]-3);
  // Flèche bas
  u8g2.drawPixel(tableau[0][menuIndex-1]-3, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18);
  u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]+18);u8g2.drawPixel(tableau[0][menuIndex-1]+3, tableau[1][menuIndex-1]+18);
  u8g2.drawPixel(tableau[0][menuIndex-1]-2, tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]+18+1);u8g2.drawPixel(tableau[0][menuIndex-1]+2, tableau[1][menuIndex-1]+18+1);
  u8g2.drawPixel(tableau[0][menuIndex-1]-1, tableau[1][menuIndex-1]+18+2);u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18+2);u8g2.drawPixel(tableau[0][menuIndex-1]+1, tableau[1][menuIndex-1]+18+2);
  u8g2.drawPixel(tableau[0][menuIndex-1], tableau[1][menuIndex-1]+18+3);

  u8g2.drawStr(0, 35, "D_");
  String sprogHourDayTemp = (progHourDayTemp < 10 ? "0" : "") + String(progHourDayTemp);
  u8g2.drawStr(24, 35, sprogHourDayTemp.c_str());
  u8g2.drawStr(44, 35, ":");
  String sProgMinuteDayTemp = (progMinuteDayTemp < 10 ? "0" : "") + String(progMinuteDayTemp);
  u8g2.drawStr(51, 35, sProgMinuteDayTemp.c_str());
  u8g2.drawStr(71, 35, "=");
  String sprogTempDayTemp = (progTempDayTemp < 9.9 ? "0" : "") + String(progTempDayTemp,1);
  u8g2.drawStr(88, 35, sprogTempDayTemp.c_str());

  u8g2.drawStr(0, 57, "N_");
  String sprogHourNightTemp = (progHourNightTemp < 10 ? "0" : "") + String(progHourNightTemp);
  u8g2.drawStr(24, 57, sprogHourNightTemp.c_str());
  u8g2.drawStr(44, 57, ":");
  String sProgMinuteNightTemp = (progMinuteNightTemp < 10 ? "0" : "") + String(progMinuteNightTemp);
  u8g2.drawStr(51, 57, sProgMinuteNightTemp.c_str());
  u8g2.drawStr(71, 57, "=");
  String sprogTempNightTemp = (progTempNightTemp < 9.9 ? "0" : "") + String(progTempNightTemp,1);
  u8g2.drawStr(88, 57, sprogTempNightTemp.c_str());
}

// Fonction affichage programation wifi
void drawWifi() {
  // dessiner le texte
  u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
  u8g2.drawStr(2, 30, "WifiProg");
}

// Fonction affichage version
void drawVersion() {
  // dessiner le texte
  u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
  u8g2.drawStr(2, 30, "Version");
}

// Fonction affichage version
void drawSave() {
  // Planifie l'affichage sans bloquer la loop
  saveMsgUntil = millis() + saveMsgDuration;
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

void handleRepeatInt(Bounce &btn, int &target, int minVal, int maxVal,
                     int step, unsigned long &pressedSince, unsigned long &lastRepeat) {
  unsigned long now = millis();

  if (btn.fell()) {
    target += step;
    if (target > maxVal) target = minVal;
    if (target < minVal) target = maxVal;
    pressedSince = now;
    lastRepeat   = now;
  }

  if (btn.read() == LOW) { // Bouton maintenu
    unsigned long held = now - pressedSince;
    unsigned long interval = 0;

    if (held > delay2) {
      interval = rate2;    // rapide
    } else if (held > delay1) {
      interval = rate1;    // lent
    }

    if (interval > 0 && now - lastRepeat >= interval) {
      target += step;
      if (target > maxVal) target = minVal;
      if (target < minVal) target = maxVal;
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

void handleWiFiReconnect(const char* ssid, const char* password) {
  static unsigned long wifiRetryTimer = 0;

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiRetryTimer == 0) {
      // Premier constat de déconnexion → on lance le chrono
      wifiRetryTimer = millis();
    } else if (millis() - wifiRetryTimer >= 60000) { // 60 sec
      Serial.println("WiFi down depuis 60s → tentative de reconnexion...");
      WiFi.begin(ssid, password);
      wifiRetryTimer = millis(); // reset pour la prochaine tentative
    }
  } else {
    // Si connecté, on remet le timer à zéro
    wifiRetryTimer = 0;
  }
}

void loop() {
  //Serial.print("Loop.");

  // Activation de l'OTA
  ArduinoOTA.handle();

  // Vérifier/reconnecter le WiFi si besoin
  handleWiFiReconnect(ssid, password);

  // Récupération de la date et de l'heure
  char date[30];
  DateTime now = rtc.now();
  if (menuState != Date) {
    //sprintf(date, "%02d/%02d/%04d %02d:%02d:%02d",
    //  now.day(), now.month(), now.year(),
    //  now.hour(), now.minute(), now.second());
    day=now.day();
    month=now.month();
    year=now.year();
    hour=now.hour();
    minute=now.minute();
  }
  sprintf(date, "%02d/%02d %02d:%02d:%02d",
    day, month, hour, minute, now.second());
  Serial.println(date);

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
  if (menuState == Accueil) {
    if (btnDroite.fell()) {
      menuState = Menu;
      if (menuIndex == 0) menuIndex = 1;
    }
    // Ajustement température quand on est en Accueil
    //if (btnHaut.fell()) tempCible += 0.1;
    //if (btnBas.fell())  tempCible -= 0.1;
    handleRepeat(btnHaut, tempCible, +0.1, hautPressedSince, hautLastRepeat);
    handleRepeat(btnBas,  tempCible, -0.1, basPressedSince,  basLastRepeat);
  } else if (menuState == Menu) {
    if (btnHaut.fell() && menuIndex > 1)   menuIndex--;
    if (btnBas.fell()  && menuIndex < 4)   menuIndex++;
    if (btnGauche.fell()) {
      menuState = Accueil;
      menuIndex = 0;
    }
    if (btnDroite.fell() && menuIndex == 1) {
      menuState = Date;
      menuIndex = 1;
    }
    if (btnDroite.fell() && menuIndex == 2) {
      menuState = Temp;
      menuIndex = 1;
      progHourDayTemp = progHourDay;
      progMinuteDayTemp = progMinuteDay;
      progTempDayTemp = progTempDay;
      progHourNightTemp = progHourNight;
      progMinuteNightTemp = progMinuteNight;
      progTempNightTemp = progTempNight;
    }
    if (btnDroite.fell() && menuIndex == 3) {
      menuState = Wifi;
      menuIndex = 1;
    }
    if (btnDroite.fell() && menuIndex == 4) {
      menuState = Version;
      menuIndex = 1;
    }
  } else if (menuState == Date) {
    if (btnGauche.fell() && menuIndex > 0)   menuIndex--;
    if (btnDroite.fell()  && menuIndex < 6)   menuIndex++;
    if (menuIndex == 0) {
      menuState = Menu;
      menuIndex = 1;
    }
    if (menuIndex == 1) {
      handleRepeatInt(btnHaut, day, 1, 31, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, day, 1, 31, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 2) {
      handleRepeatInt(btnHaut, month, 1, 12, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, month, 1, 12, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 3) {
      handleRepeatInt(btnHaut, year, 1, 9999, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, year, 1, 9999, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 4) {
      handleRepeatInt(btnHaut, hour, 0, 23, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, hour, 0, 23, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 5) {
      handleRepeatInt(btnHaut, minute, 0, 59, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, minute, 0, 59, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 6) {
      drawSave();
      DateTime nouvelleDate(year, month, day, hour, minute, 0);
      rtc.adjust(nouvelleDate);
      menuState = Accueil;
      menuIndex = 0;
    }
  } else if (menuState == Temp) {
    if (btnGauche.fell() && menuIndex > 0)   menuIndex--;
    if (btnDroite.fell()  && menuIndex < 7)   menuIndex++;
    if (menuIndex == 0) {
      menuState = Menu;
      menuIndex = 2;
    }
    if (menuIndex == 1) {
      handleRepeatInt(btnHaut, progHourDayTemp, 0, 23, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, progHourDayTemp, 0, 23, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 2) {
      handleRepeatInt(btnHaut, progMinuteDayTemp, 0, 59, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, progMinuteDayTemp, 0, 59, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 3) {
      handleRepeat(btnHaut, progTempDayTemp, +0.1, hautPressedSince, hautLastRepeat);
      handleRepeat(btnBas,  progTempDayTemp, -0.1, basPressedSince,  basLastRepeat);
    }
    if (menuIndex == 4) {
      handleRepeatInt(btnHaut, progHourNightTemp, 0, 23, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, progHourNightTemp, 0, 23, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 5) {
      handleRepeatInt(btnHaut, progMinuteNightTemp, 0, 59, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas, progMinuteNightTemp, 0, 59, -1, basPressedSince, basLastRepeat);
    }
    if (menuIndex == 6) {
      handleRepeat(btnHaut, progTempNightTemp, +0.1, hautPressedSince, hautLastRepeat);
      handleRepeat(btnBas,  progTempNightTemp, -0.1, basPressedSince,  basLastRepeat);
    }
    if (menuIndex == 7) {
      drawSave();
      menuState = Accueil;
      menuIndex = 0;
      progHourDay = progHourDayTemp;
      progMinuteDay = progMinuteDayTemp;
      progTempDay = progTempDayTemp;
      progHourNight = progHourNightTemp;
      progMinuteNight = progMinuteNightTemp;
      progTempNight = progTempNightTemp;
    }
  } else if (menuState == Wifi) {
    if (btnGauche.fell() && menuIndex > 0)   menuIndex--;
    if (btnDroite.fell()  && menuIndex < 1)   menuIndex++;
    if (menuIndex == 0) {
      menuState = Menu;
      menuIndex = 3;
    }
  } else if (menuState == Version) {
    if (btnGauche.fell() && menuIndex > 0)   menuIndex--;
    if (btnDroite.fell()  && menuIndex < 1)   menuIndex++;
    if (menuIndex == 0) {
      menuState = Menu;
      menuIndex = 4;
    }
  }

  u8g2.clearBuffer(); // efface le buffer
  if (menuState == Accueil) {
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
      digitalWrite(PIN_RELAY, HIGH);  // relais ON
    } else {
      digitalWrite(PIN_RELAY, LOW);   // relais OFF
    }

    // Affichage du signal wifi
    int x=120;
    int y=10;
    drawWiFiIcon(u8g2, x, y, rssi);
    //u8g2.setFont(u8g2_font_t0_12_tf);
    //u8g2.drawStr(11, 16, (String(rssi)).c_str());
    if (saveMsgUntil && ((long)saveMsgUntil - (long)millis()) > 0) {
      u8g2.setFont(u8g2_font_fub11_tr);
      u8g2.drawStr(25, 64, "Saved !");
    } else if (saveMsgUntil) {
      saveMsgUntil = 0;
    }
  } else if (menuState == Menu) {
      drawMenu();
  } else if (menuState == Date) {
      drawDate();
  } else if (menuState == Temp) {
      drawTemp();
  } else if (menuState == Wifi) {
      drawWifi();
  } else if (menuState == Version) {
      drawVersion();
  }
  u8g2.sendBuffer(); // envoie à l'écran
  //delay(2000);
}