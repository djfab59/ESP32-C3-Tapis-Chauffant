#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Bounce2.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128
#define OLED_ADDR 0x3C

#define I2C_SDA D10
#define I2C_SCL D9

TwoWire I2Cone = TwoWire(0);
Adafruit_SH1107 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cone);

// Broches des boutons
#define BTN_BAS    D0
#define BTN_GAUCHE D1
#define BTN_HAUT   D2
#define BTN_DROITE D3

// Bounce2 pour chaque bouton
Bounce btnHaut   = Bounce();
Bounce btnBas    = Bounce();
Bounce btnGauche = Bounce();
Bounce btnDroite = Bounce();

// Variables d'état
int menuIndex = 0;     // 0=Accueil, 1=Heure, 2=Ctrl Temp, 3=Version
bool inMenu = false;

// Variables température
float tempAct = 25.5;  // Température actuelle
float tempCible = 26.0; // Température à atteindre

void setup() {
  Serial.begin(115200);

  pinMode(D7, OUTPUT);
  digitalWrite(D7, HIGH);

  pinMode(BTN_HAUT,   INPUT_PULLUP);
  pinMode(BTN_BAS,    INPUT_PULLUP);
  pinMode(BTN_GAUCHE, INPUT_PULLUP);
  pinMode(BTN_DROITE, INPUT_PULLUP);

  btnHaut.attach(BTN_HAUT);     btnHaut.interval(25);
  btnBas.attach(BTN_BAS);       btnBas.interval(25);
  btnGauche.attach(BTN_GAUCHE); btnGauche.interval(25);
  btnDroite.attach(BTN_DROITE); btnDroite.interval(25);

  I2Cone.begin(I2C_SDA, I2C_SCL, 400000);

  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("Échec initialisation SH1107");
    for (;;);
  }

  display.setRotation(0);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 20);
  display.println("Pret !");
  display.display();
}

// Fonction affichage menu
void drawMenu() {
  const char* items[] = {"Heure", "Ctl Temp", "Version"};
  for (int i = 1; i <= 3; i++) {
    int y = 20 + (i - 1) * 20;
    if (i == menuIndex) {
      display.fillRect(0, y, 128, 18, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setCursor(0, y);
    display.setTextSize(2);
    display.println(items[i - 1]);
  }
}

void loop() {
// Contrôle relais D7 selon température
  if (tempAct >= tempCible) {
    digitalWrite(D7, LOW);   // relais désactivé / chauffage éteint
  } else {
    digitalWrite(D7, HIGH);  // relais activé / chauffage allumé
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
    if (btnHaut.fell()) tempCible += 0.1;
    if (btnBas.fell())  tempCible -= 0.1;
  }

  // Affichage
  display.clearDisplay();
  if (!inMenu) {
    display.setCursor(0, 60);
    display.setTextSize(4);
    display.println(tempAct, 1);  // 1 chiffre après la virgule

    display.setCursor(7, 100);
    display.setTextSize(2);
    display.println(tempCible, 1);
  } else {
    drawMenu();
  }
  display.display();
}
