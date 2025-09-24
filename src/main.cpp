#include <U8g2lib.h>
#include <Wire.h>
#include <Bounce2.h>
#include <DallasTemperature.h>
#include <ArduinoOTA.h>
#include <RTClib.h>
#include <cstring>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "pins.h"
#include <regex>

//Broches + Screen centralisées dans include/pins.h
// Utilisation du constructeur SH1106 pour ton clone
//U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Save
Preferences prefs;

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

// variable long press
unsigned long droitePressedAt = 0;
const unsigned long LONG_PRESS_MS = 2000;

// Affichage non bloquant du message "Saved !"
unsigned long saveMsgUntil = 0;                 // moment où on cesse l'affichage
const unsigned long saveMsgDuration = 2000;     // durée d'affichage en ms

// Parametre de Prise de température
const unsigned long tempDelay = 1000; // toutes les 1s

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

// Variables d'état du menu wifi
enum WifiSubState {
  WifiMain,
  WifiScan,
  WifiSSID,
  WifiPassword,
  WifiSave
};
WifiSubState wifiState = WifiMain;
int wifiCount = 0;          // nombre de réseaux trouvés
String wifiSSID = "Wokwi-GUEST";
String wifiPass = "";
String wifiSSIDTemp, wifiPassTemp;
int charIndex = 0;          // index du caractère courant pour saisie pass
const char charSet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-*$&@";

// Variables date
int day = 30, month = 12, year = 2025;
int hour = 23, minute = 59;

// Variables température
float tempAct = 25.5;  // Température actuelle
float tempCible = 25.0; // Température à atteindre

// Variable Prog Temp
int progHourDay = 9;
int progMinuteDay = 30;
float progTempDay = 25.5;
int progHourNight = 19;
int progMinuteNight = 0;
float progTempNight = 20.5;
int progHourDayTemp, progMinuteDayTemp, progHourNightTemp, progMinuteNightTemp;
float progTempDayTemp, progTempNightTemp;

// Variable forcage manuel de la température
bool manualTemp = false;

// Durée de transition (2h = 120 minutes)
const int fadeDuration = 120;

// Update
const char* currentVersion = "0.1";
const char* manifestURL = "https://raw.githubusercontent.com/djfab59/ESP32-C3-Tapis-Chauffant/refs/heads/master/release/version.json";

void setup() {
  Serial.begin(9600);
  Serial.print("Setup!");

  // Initialisation des préférences
  // Ouvre un "namespace" appelé "config"
  prefs.begin("config", true);
  // Récupère les valeurs stockées, sinon met la valeur par défaut
  progHourDay   = prefs.getInt("hourDay",   progHourDay);
  progMinuteDay = prefs.getInt("minDay",    progMinuteDay);
  progTempDay   = prefs.getFloat("tempDay", progTempDay);
  progHourNight   = prefs.getInt("hourNight",   progHourNight);
  progMinuteNight = prefs.getInt("minNight",    progMinuteNight);
  progTempNight   = prefs.getFloat("tempNight", progTempNight);
  // Ferme les préférences
  prefs.end();
  prefs.begin("wifi", true);
  // Récupère les valeurs stockées, sinon met la valeur par défaut
  wifiSSID = prefs.getString("wifiSSID", wifiSSID);
  wifiPass = prefs.getString("wifiPass", wifiPass);
  // Ferme les préférences
  prefs.end();

  // Initialisation du capteur de température
  ds.begin();
  ds.setResolution(12); // 11 bits → 375 ms → 0.125 °C , 12 bits → 750 ms → 0.0625 °C
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
  // Éteint le relais
  digitalWrite(PIN_RELAY, LOW);

  // Initialisation du Wifi
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 8, "Wifi connecting...");
  WiFi.begin(wifiSSID, wifiPass);
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

// Fonction dessin flèche haut/bas
void drawArrow (int x, int y, int fontSize, int nbCharacter) {
  //calcul de la position de la flèche par rapport à la taille de la police est du nombre de charactère
  int y_margeHaut, y_margeBas;
  if (fontSize==11) {
  	x = x + 9 * ( nbCharacter / 2 );
  	//y = y - 15;
  	y_margeHaut = -15;
  	y_margeBas = 3;
  } else if (fontSize==8) {
  	x = x + 3;
  	//y = y - 15;
  	y_margeHaut = -12;
  	y_margeBas = 5;
  } else {
  	x = x + 9 * ( nbCharacter / 2 );
  	//y = y - 15;
  	y_margeHaut = -18;
  	y_margeBas = 3;
  }
  // Flèche haut
  for (int dx=-3; dx<=3; dx++) {
    u8g2.drawPixel(x+dx, y+y_margeHaut);
  }
  for (int dx=-2; dx<=2; dx++) {
    u8g2.drawPixel(x+dx, y+y_margeHaut-1);
  }
  for (int dx=-1; dx<=1; dx++) {
    u8g2.drawPixel(x+dx, y+y_margeHaut-2);
  }
  u8g2.drawPixel(x, y+y_margeHaut-3);

  // Flèche bas
  for (int dx=-3; dx<=3; dx++) {
    u8g2.drawPixel(x+dx, y+y_margeBas);
  }
  for (int dx=-2; dx<=2; dx++) {
    u8g2.drawPixel(x+dx, y+y_margeBas+1);
  }
  for (int dx=-1; dx<=1; dx++) {
    u8g2.drawPixel(x+dx, y+y_margeBas+2);
  }
  u8g2.drawPixel(x, y+y_margeBas+3);
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

  // Affichage date
  String sday = (day < 10 ? "0" : "") + String(day);
  u8g2.drawStr(22, 35, sday.c_str());
  if (menuIndex==1) drawArrow(22,35,11,2);
  u8g2.drawStr(42, 35, "/");
  String smonth = (month < 10 ? "0" : "") + String(month);
  u8g2.drawStr(49, 35, smonth.c_str());
  if (menuIndex==2) drawArrow(49,35,11,2);
  u8g2.drawStr(69, 35, "/");
  String syear = String(year);
  u8g2.drawStr(76, 35, syear.c_str());
  if (menuIndex==3) drawArrow(76,35,11,4);

  // Affichage heure
  String shour = (hour < 10 ? "0" : "") + String(hour);
  u8g2.drawStr(40, 57, shour.c_str());
  if (menuIndex==4) drawArrow(40,57,11,2);
  u8g2.drawStr(60, 57, ":");
  String sminute = (minute < 10 ? "0" : "") + String(minute);
  u8g2.drawStr(66, 57, sminute.c_str());
  if (menuIndex==5) drawArrow(66,57,11,2);

  u8g2.setFont(u8g2_font_open_iconic_check_1x_t);
  u8g2.drawGlyph(100, 57, 0x0040);
}

// Fonction affichage programation température
void drawTemp() {
  u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
  u8g2.drawStr(30, 11, "TempProg");

  // Affichage programmation jour
  u8g2.drawStr(0, 35, "D_");
  String sprogHourDayTemp = (progHourDayTemp < 10 ? "0" : "") + String(progHourDayTemp);
  u8g2.drawStr(24, 35, sprogHourDayTemp.c_str());
  if (menuIndex==1) drawArrow(24,35,11,2);
  u8g2.drawStr(44, 35, ":");
  String sProgMinuteDayTemp = (progMinuteDayTemp < 10 ? "0" : "") + String(progMinuteDayTemp);
  u8g2.drawStr(51, 35, sProgMinuteDayTemp.c_str());
  if (menuIndex==2) drawArrow(51,35,11,2);
  u8g2.drawStr(71, 35, "=");
  String sprogTempDayTemp = (progTempDayTemp < 9.9 ? "0" : "") + String(progTempDayTemp,1);
  u8g2.drawStr(88, 35, sprogTempDayTemp.c_str());
  if (menuIndex==3) drawArrow(88,35,11,4);

  // Affichage programmation nuit
  u8g2.drawStr(0, 57, "N_");
  String sprogHourNightTemp = (progHourNightTemp < 10 ? "0" : "") + String(progHourNightTemp);
  u8g2.drawStr(24, 57, sprogHourNightTemp.c_str());
  if (menuIndex==4) drawArrow(24,57,11,2);
  u8g2.drawStr(44, 57, ":");
  String sProgMinuteNightTemp = (progMinuteNightTemp < 10 ? "0" : "") + String(progMinuteNightTemp);
  u8g2.drawStr(51, 57, sProgMinuteNightTemp.c_str());
  if (menuIndex==5) drawArrow(51,57,11,2);
  u8g2.drawStr(71, 57, "=");
  String sprogTempNightTemp = (progTempNightTemp < 9.9 ? "0" : "") + String(progTempNightTemp,1);
  u8g2.drawStr(88, 57, sprogTempNightTemp.c_str());
  if (menuIndex==6) drawArrow(88,57,11,4);
}

// Fonction interpolation en S (cosinus)
float smoothStep(float startTemp, float endTemp, int startMinute, int endMinute, int nowMinute) {
  // Cas avant/après la période
  if (nowMinute < startMinute) return endTemp;
  if (nowMinute > endMinute)   return endTemp;

  float ratio = float(nowMinute - startMinute) / float(endMinute - startMinute);
  float sCurve = (1 - cos(ratio * PI)) / 2.0; // courbe en S
  return startTemp + sCurve * (endTemp - startTemp);
}

// Calcul de la température cible
float getTempCible(DateTime now) {
  int hour   = now.hour();
  int minute = now.minute();
  int minuteNow   = hour * 60 + minute;
  int minuteDay   = progHourDay   * 60 + progMinuteDay;
  int minuteNight = progHourNight * 60 + progMinuteNight;

  // Cas normal (jour sans minuit)
  if (minuteDay < minuteNight) {
    if (minuteNow >= minuteDay && minuteNow < minuteNight) {
      // On est en jour -> interpoler depuis la nuit
      return smoothStep(progTempNight, progTempDay,
                        minuteDay, minuteDay + fadeDuration, minuteNow);
    } else {
      // On est en nuit -> interpoler depuis le jour
      return smoothStep(progTempDay, progTempNight,
                        minuteNight, minuteNight + fadeDuration, minuteNow);
    }
  } else {
    // Cas qui traverse minuit
    if (minuteNow >= minuteDay || minuteNow < minuteNight) {
      // Jour
      return smoothStep(progTempNight, progTempDay,
                        minuteDay, (minuteDay + fadeDuration) % (24*60), minuteNow);
    } else {
      // Nuit
      return smoothStep(progTempDay, progTempNight,
                        minuteNight, (minuteNight + fadeDuration) % (24*60), minuteNow);
    }
  }
}

// Fonction affichage programation wifi
void drawWifi() {
  if (wifiState == WifiMain) {
    const char* items[] = {"Scan SSID", "Fixe SSID", "WPA", "Save"};
    for (int i = 0; i < 4; i++) {
      int y = 15 + i*15;
      if (menuIndex == i+1 ) {
        u8g2.drawBox(0, y-12, 128, 14);
        u8g2.setDrawColor(0);
      } else {
        u8g2.setDrawColor(1);
      }
      u8g2.drawStr(2, y, items[i]);
      u8g2.setDrawColor(1);
    }
  }

  else if (wifiState == WifiScan) {
    u8g2.setFont(u8g2_font_fub11_tr);
    u8g2.drawStr(45, 11, "SSID:");
    //u8g2.drawStr(110, 11, String(wifiCount).c_str());
    u8g2.setFont(u8g2_font_ncenB08_tf);

    // Si menuIndex est inférieur à 4, on affiche les 5 premiers SSID
    if (menuIndex < 5){
      for (int i = 0; i < 5 ; i++) {
        int y = 11 + 2 + 9 + i * 10;
        if (i == menuIndex) {
          u8g2.drawBox(0, y-9, 128, 10);
          u8g2.setDrawColor(0);
        } else {
          u8g2.setDrawColor(1);
        }
        u8g2.drawStr(2, y, WiFi.SSID(i).c_str());
        u8g2.setDrawColor(1);
      }
    } else { // sinon affiche le SSID sélectionné et les 4 derniers
      for (int i = menuIndex; i > menuIndex - 4; i--) {
        int y = 11 + 2 + 9 + 4 * 10 - (menuIndex-i) * 10;
        if (i == menuIndex) {
          u8g2.drawBox(0, y-9, 128, 10);
          u8g2.setDrawColor(0);
        } else {
          u8g2.setDrawColor(1);
        }
        u8g2.drawStr(2, y, WiFi.SSID(i).c_str());
        u8g2.setDrawColor(1);
      }
    }
  }

  else if (wifiState == WifiSSID)
  {
    u8g2.setFont(u8g2_font_fub11_tr);
    u8g2.drawStr(23, 11, "SSID:");
    u8g2.setFont(u8g2_font_ncenB08_tf);
    char current[2] = { charSet[charIndex], 0 };
    // u8g2.getStrWidth(wifiSSID.c_str() pour connaitre la position de la taille de la chaine
    int large=10;
    // Si l'écran est plus grand que le texte
    if (u8g2.getStrWidth(wifiSSIDTemp.c_str()) < 128-large) {
      u8g2.drawStr(0, 39, wifiSSIDTemp.c_str());
      u8g2.drawStr(u8g2.getStrWidth(wifiSSIDTemp.c_str())+1, 39, current);
      drawArrow(u8g2.getStrWidth(wifiSSIDTemp.c_str())+1,39,8,1);
    } else {
      int x=u8g2.getStrWidth(wifiSSIDTemp.c_str());
      u8g2.drawStr(128-1-x-large, 39, wifiSSIDTemp.c_str());
      u8g2.drawStr(128-large, 39, current);
      drawArrow(128-large,39,8,1);
    }
  }

  else if (wifiState == WifiPassword) {
    u8g2.setFont(u8g2_font_fub11_tr);
    u8g2.drawStr(13, 11, "WPAssword:");
    u8g2.setFont(u8g2_font_ncenB08_tf);

    char current[2] = { charSet[charIndex], 0 };
    int large=10;
    // Si l'écran est plus grand que le texte
    if (u8g2.getStrWidth(wifiPassTemp.c_str()) < 128-large) {
      u8g2.drawStr(0, 39, wifiPassTemp.c_str());
      u8g2.drawStr(u8g2.getStrWidth(wifiPassTemp.c_str())+1, 39, current);
      drawArrow(u8g2.getStrWidth(wifiPassTemp.c_str())+1,39,8,1);
    } else {
      int x=u8g2.getStrWidth(wifiPassTemp.c_str());
      u8g2.drawStr(128-1-x-large, 39, wifiPassTemp.c_str());
      u8g2.drawStr(128-large, 39, current);
      drawArrow(128-large,39,8,1);
    }
  }
}

void testIp() {
  HTTPClient http;
  if (!http.begin("http://monip.org")) {
    Serial.println("Impossible d'init HTTP");
    return;
  }

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    http.end();

    // --- extraction IP avec regex ---
    std::regex ipRegex("\\b([0-9]{1,3}\\.){3}[0-9]{1,3}\\b");
    std::smatch match;
    std::string sPayload = payload.c_str();

    if (std::regex_search(sPayload, match, ipRegex)) {
      String ip = match.str().c_str();
      Serial.printf("IP publique : %s\n", ip.c_str());
    } else {
      Serial.println("Impossible d'extraire l'IP publique");
    }
  } else {
    Serial.printf("Erreur HTTP %d\n", httpCode);
    http.end();
  }
}

void testIp2() {
  WiFiClientSecure client;
  client.setInsecure();  // ⚠️ comme curl -k → accepte tout certificat

  HTTPClient https;
  //if (!https.begin(client, "https://api.ipify.org?format=json")) {
  //if (!https.begin(client, "https://api.ipify.org")) {
  if (!https.begin(client, "https://api.ipify.org/?format=json")) {
    Serial.println("Impossible d'init HTTPS");
    return;
  }

  int httpCode = https.GET();
  if (httpCode == 200) {
    String payload = https.getString();
    https.end();

    // --- Parsing JSON ---
    JsonDocument doc;
    //doc.capacity(256);  // suffisant pour {"ip":"x.x.x.x"}

    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.printf("Erreur JSON: %s\n", err.c_str());
      return;
    }

    String ip = doc["ip"] | "";
    if (ip.length()) {
      Serial.printf("IP publique : %s\n", ip.c_str());
    } else {
      Serial.println("Clé 'ip' absente du JSON");
    }
  } else {
    Serial.printf("Erreur HTTP %d\n", httpCode);
    https.end();
  }
}

// Fonction pour vérifier les mises à jour
void checkUpdate() {
  WiFiClientSecure client;
  client.setInsecure();  // ⚠️ équivalent curl -k (pas de vérification TLS)

  HTTPClient https;
  if (!https.begin(client, manifestURL)) {
    Serial.println("Impossible d'init la connexion HTTPS");
    return;
  }

  int httpCode = https.GET();
  if (httpCode == 200) {
    String payload = https.getString();
    https.end();

    // --- Parsing JSON ---
    //JsonDocument doc;
    //doc.capacity(1024); 
    StaticJsonDocument<1024> doc;

    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.printf("Erreur JSON: %s\n", err.c_str());
      return;
    }

    String latest   = doc["latest"]   | "";
    String rollback = doc["rollback"] | "";
    String latestURL = doc["firmwares"][latest]["url"] | "";

    if (latest.length() == 0 || latestURL.length() == 0) {
      Serial.println("Manifest JSON incomplet !");
      return;
    }

    if (latest != currentVersion) {
      Serial.printf("Nouvelle version %s dispo, mise à jour...\n", latest.c_str());

      t_httpUpdate_return ret = httpUpdate.update(client, latestURL);
      switch (ret) {
        case HTTP_UPDATE_FAILED:
          Serial.printf("Echec update: %s\n", httpUpdate.getLastErrorString().c_str());
          if (rollback.length()) {
            String rollbackURL = doc["firmwares"][rollback]["url"] | "";
            if (rollbackURL.length()) {
              Serial.println("Tentative rollback...");
              httpUpdate.update(client, rollbackURL);
            }
          }
          break;

        case HTTP_UPDATE_NO_UPDATES:
          Serial.println("Pas de mise à jour.");
          break;

        case HTTP_UPDATE_OK:
          Serial.println("Update réussie !");
          break;
      }
    } else {
      Serial.println("Firmware déjà à jour.");
    }
  } else {
    Serial.printf("Erreur HTTP %d\n", httpCode);
    https.end();
  }
}

// Fonction affichage version
void drawVersion() {
  // dessiner le texte
  u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
  u8g2.drawStr(2, 30, "Version");
  checkUpdate();
  //testIp();
  //testIp2();
}

// Fonction affichage version
void drawSave() {
  // Planifie l'affichage sans bloquer la loop
  saveMsgUntil = millis() + saveMsgDuration;
}

// Fonction générique d’auto-repeat
// Retourne true si "target" a été modifiée
bool handleRepeat(Bounce &btn, float &target, float step, unsigned long &pressedSince, unsigned long &lastRepeat) {
  bool changed = false;
  unsigned long now = millis();

  if (btn.fell()) {
    // Action immédiate
    target += step;
    pressedSince = now;
    lastRepeat   = now;
    changed = true;
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
      changed = true;
    }
  }
  return changed;
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

void handleWiFiReconnect(String ssid, String password) {
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
  handleWiFiReconnect(wifiSSID, wifiPass);

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

  // Récupération de la température cible
  //tempCible = getTempCible(rtc.now());
  if (!manualTemp) tempCible = getTempCible(rtc.now());

  // Mise à jour de la tempéraure
  // Timer pour la température
  static unsigned long lastRequest = 0;
  if (millis() - lastRequest > tempDelay) {
    ds.requestTemperatures(); 
    lastRequest = millis();
  }
  float t = ds.getTempCByIndex(0);   // on lit la dernière valeur dispo (sans bloquer)
  if (t == DEVICE_DISCONNECTED_C) {
    tempAct=66.6;
  } else {
    tempAct=t;
  }

  // Activation du chauffage
  if (tempAct<tempCible)
  {
    digitalWrite(PIN_RELAY, HIGH);  // relais ON
  } else {
    digitalWrite(PIN_RELAY, LOW);   // relais OFF
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
    // Passe en manuel si la consigne change
    //if (btnHaut.fell()) tempCible += 0.1;
    //if (btnBas.fell())  tempCible -= 0.1;
    if (handleRepeat(btnHaut, tempCible, +0.1, hautPressedSince, hautLastRepeat)) {
      manualTemp = true;
    }
    if (handleRepeat(btnBas,  tempCible, -0.1, basPressedSince,  basLastRepeat)) {
      manualTemp = true;
    }
    if (btnGauche.fell()) {
      manualTemp = false;
    }
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
      wifiState = WifiMain;
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
      // Mise à jour des valeurs
      progHourDay = progHourDayTemp;
      progMinuteDay = progMinuteDayTemp;
      progTempDay = progTempDayTemp;
      progHourNight = progHourNightTemp;
      progMinuteNight = progMinuteNightTemp;
      progTempNight = progTempNightTemp;
      // Sauvegarde dans les préférences
      prefs.begin("config", false);
      prefs.putInt("hourDay", progHourDay);
      prefs.putInt("minDay", progMinuteDay);
      prefs.putFloat("tempDay", progTempDay);
      prefs.putInt("hourNight", progHourNight);
      prefs.putInt("minNight", progMinuteNight);
      prefs.putFloat("tempNight", progTempNight);
      prefs.end();
    }
  } else if (menuState == Wifi) {
    if (wifiState == WifiMain){
      if (btnHaut.fell() && menuIndex > 1)   menuIndex--;
      if (btnBas.fell()  && menuIndex < 4)   menuIndex++;
      if (btnGauche.fell()) {
        menuState = Menu;
        menuIndex = 3;
      }
      if (menuIndex == 1 && btnDroite.fell()) {
        wifiState = WifiScan;
        u8g2.clearBuffer(); // efface le buffer
        u8g2.setFont(u8g2_font_fub11_tr); // choisir police adaptée
        u8g2.drawStr(40, 38, "Wait ...");
        u8g2.sendBuffer();
        wifiCount = WiFi.scanNetworks();
        menuIndex = 0;
      }
      if (menuIndex == 2 && btnDroite.fell()) {
        wifiState = WifiSSID;
      }
      if (menuIndex == 3 && btnDroite.fell()) {
        wifiState = WifiPassword;
      }
      if (menuIndex == 4 && btnDroite.fell()) {
        wifiSSID = wifiSSIDTemp;
        wifiPass = wifiPassTemp;
        // Sauvegarde dans les préférences
        prefs.begin("wifi", false);
        prefs.putString("wifiSSID", wifiSSID);
        prefs.putString("wifiPass", wifiPass);
        prefs.end();
        menuState = Accueil;
        menuIndex = 0;
        drawSave();
      }
    } else if (wifiState == WifiScan) {
      if (btnGauche.fell()) {
        wifiState = WifiMain;
        menuIndex = 1;
      }
      if (btnHaut.fell() && menuIndex > 0) menuIndex--;
      if (btnBas.fell() && menuIndex < wifiCount-1) menuIndex++;
      if (btnDroite.fell()) {
        wifiSSIDTemp = WiFi.SSID(menuIndex);
        wifiState = WifiMain;
        menuIndex = 3;
      }
    } else if (wifiState == WifiSSID) {
      int nChars = sizeof(charSet) - 1; // nombre de caractères dispo
      handleRepeatInt(btnHaut, charIndex, 0, nChars-1, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas,  charIndex, 0, nChars-1, -1, basPressedSince,  basLastRepeat);
      if (btnGauche.fell()) {
        if (wifiSSIDTemp.length() > 0) wifiSSIDTemp.remove(wifiSSIDTemp.length()-1);
        else {
          wifiState = WifiMain; // sortie
          menuIndex = 2;
        }
      }
      // Déclanchement du chrono à l'appuie
      if (btnDroite.fell()) {
        droitePressedAt = millis();
      }
      // Gestion du court appui
      if (btnDroite.rose()) {  // relaché
        // Si relâché avant 2s → appui court
        if (droitePressedAt != 0) {
          wifiSSIDTemp += charSet[charIndex]; // Ajjout du character
          droitePressedAt = 0; // Reset pour éviter répétition
        }
      }
      // Gestion du long appui
      if (btnDroite.read() == LOW && droitePressedAt != 0) {
        if (millis() - droitePressedAt >= LONG_PRESS_MS) {
          droitePressedAt = 0;       // Reset pour éviter répétition
          wifiState = WifiMain; // sortie directe après 2s d'appui
          menuIndex = 3;
        }
      }
    } else if (wifiState == WifiPassword) {
      int nChars = sizeof(charSet) - 1; // nombre de caractères dispo
      handleRepeatInt(btnHaut, charIndex, 0, nChars-1, +1, hautPressedSince, hautLastRepeat);
      handleRepeatInt(btnBas,  charIndex, 0, nChars-1, -1, basPressedSince,  basLastRepeat);
      if (btnGauche.fell()) {
        if (wifiPassTemp.length() > 0) wifiPassTemp.remove(wifiPassTemp.length()-1);
        else {
          wifiState = WifiMain; // sortie
          menuIndex = 3;
        }
      }
      // Déclanchement du chrono à l'appuie
      if (btnDroite.fell()) {
        droitePressedAt = millis();
      }
      // Gestion du court appui
      if (btnDroite.rose()) {  // relaché
        // Si relâché avant 2s → appui court
        if (droitePressedAt != 0) {
          wifiPassTemp += charSet[charIndex]; // Ajjout du character
          droitePressedAt = 0; // Reset pour éviter répétition
        }
      }
      // Gestion du long appui
      if (btnDroite.read() == LOW && droitePressedAt != 0) {
        if (millis() - droitePressedAt >= LONG_PRESS_MS) {
          droitePressedAt = 0;       // Reset pour éviter répétition
          wifiState = WifiMain; // sortie directe après 2s d'appui
          menuIndex = 4;
        }
      }
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
    // Affichage d'une icone cadenat si forcage manuel de la température
    if (manualTemp) {
      u8g2.setFont(u8g2_font_open_iconic_thing_1x_t);
      u8g2.drawGlyph(86, 65, 0x004f);
    }

    // Affichage de l'icône de chauffage
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
