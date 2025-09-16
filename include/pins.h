// Centralisation des broches pour HW et Wokwi
// Change uniquement ici (ou via build_flags) selon la cible.
// Utilisation: inclure ce fichier et référencer PIN_*

#pragma once

// Sélection de cible via build_flags: -D TARGET_WOKWI
// Par défaut: cible matériel réel (TARGET_HW implicite)

#if defined(TARGET_WOKWI)
  // Broches pour Wokwi (adapter si besoin à votre diagram.json)
  static const int PIN_SDA       = D10;
  static const int PIN_SCL       = D9;
  static const int PIN_BTN_BAS   = D0;
  static const int PIN_BTN_GAUCHE= D1;
  static const int PIN_BTN_HAUT  = D2;
  static const int PIN_BTN_DROITE= D3;
  static const int PIN_RELAY     = D7;
  static const int PIN_ONEWIRE   = D8; // DS18B20
  // Utilisation du constructeur SSD1306 pour Wokwi
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#else
  // Broches pour le matériel (mêmes valeurs par défaut que le code actuel)
  static const int PIN_SDA       = 3;
  static const int PIN_SCL       = 4;
  static const int PIN_BTN_BAS   = 9;
  static const int PIN_BTN_GAUCHE= 10;
  static const int PIN_BTN_HAUT  = 20;
  static const int PIN_BTN_DROITE= 21;
  static const int PIN_RELAY     = D7;
  static const int PIN_ONEWIRE   = 2; // DS18B20
  // Utilisation du constructeur SH1106 pour ton clone
  U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#endif

