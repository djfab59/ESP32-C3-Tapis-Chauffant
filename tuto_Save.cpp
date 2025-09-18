#include <Preferences.h>

Preferences prefs;

// Variables
int progHourDay   = 9;
int progMinuteDay = 30;
float progTempDay = 25.5;
int progHourNight = 19;
int progMinuteNight = 0;
float progTempNight = 20.5;

void setup() {
  Serial.begin(115200);

  // Ouvre un "namespace" appelé "config"
  prefs.begin("config", false);

  // Récupère les valeurs stockées, sinon met la valeur par défaut
  progHourDay   = prefs.getInt("hourDay",   9);
  progMinuteDay = prefs.getInt("minDay",   30);
  progTempDay   = prefs.getFloat("tempDay", 25.5);
  progHourNight = prefs.getInt("hourNight", 19);
  progMinuteNight = prefs.getInt("minNight", 0);
  progTempNight   = prefs.getFloat("tempNight", 20.5);

  // Affichage
  Serial.printf("Day: %02d:%02d -> %.1f°C\n", progHourDay, progMinuteDay, progTempDay);
  Serial.printf("Night: %02d:%02d -> %.1f°C\n", progHourNight, progMinuteNight, progTempNight);

  prefs.end();
}

void loop() {
  // Exemple : si tu changes une valeur
  if (false) { // remplace par une condition
    prefs.begin("config", false);
    prefs.putInt("hourDay", progHourDay);
    prefs.putInt("minDay", progMinuteDay);
    prefs.putFloat("tempDay", progTempDay);
    prefs.putInt("hourNight", progHourNight);
    prefs.putInt("minNight", progMinuteNight);
    prefs.putFloat("tempNight", progTempNight);
    prefs.end();
    Serial.println("Config sauvegardée !");
  }
}
