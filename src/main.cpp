#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128
#define OLED_ADDR 0x3C

#define I2C_SDA D10
#define I2C_SCL D9

TwoWire I2Cone = TwoWire(0);
Adafruit_SH1107 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cone);

bool afficher = true;

void setup() {
  Serial.begin(115200);

  // Déclaration de D7 comme output et mise à 1
  pinMode(D7, OUTPUT);
  digitalWrite(D7, HIGH);

  // Initialisation du bus I2C
  I2Cone.begin(I2C_SDA, I2C_SCL, 400000);

  // Initialisation SH1107
  if (!display.begin(OLED_ADDR, true)) {   // true = reset hardware si dispo
    Serial.println("Échec initialisation SH1107");
    for (;;);
  }

  display.setRotation(0); // 0 à 3
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("Coucou !");
  display.display();
}

void loop() {
  display.clearDisplay();

  if (afficher) {
    display.setCursor(0, 0);
    display.println("Coucou !");
  }

  display.display();
  afficher = !afficher;
  delay(1000);
}
