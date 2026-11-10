/*
  Waage mit Arduino Pro Mini (ATmega328P, 3,3 V, 8 MHz)
  HX711 + 0,96" OLED SSD1306 (I2C) + 2 Taster
  Versorgung: 9-V-Block -> Spannungsregler -> VCC

  Verdrahtung:
    HX711 DOUT  -> D4
    HX711 SCK   -> D5
    OLED SDA    -> A4
    OLED SCL    -> A5
    Taster An/Tara -> D2 und GND
    Taster Aus     -> D3 und GND
    Batteriemessung: 9V+ -> 1 MOhm -> A0 -> 100 kOhm -> GND
                     zusaetzlich 1-100 nF von A0 nach GND
    Alle GND verbinden

  Serieller Monitor (9600 Baud):
    t      = tarieren
    k500   = kalibrieren mit 500 g Referenzgewicht (Zahl anpassen)
    b9.12  = Batterieanzeige abgleichen (mit Multimeter gemessene Volt)
    f      = aktuelle Kalibrierwerte anzeigen
*/

#include <Wire.h>
#include <U8g2lib.h>
#include <HX711.h>
#include <EEPROM.h>
#include <avr/sleep.h>

// ---------------- Einstellungen ----------------
const byte PIN_HX_DOUT = 4;
const byte PIN_HX_SCK  = 5;
const byte PIN_TARA    = 2;   // muss D2 bleiben (Interrupt zum Aufwecken)
const byte PIN_AUS     = 3;
const byte PIN_BAT     = A0;

const float DEFAULT_FACTOR       = 1000.0f;  // Startwert vor Kalibrierung
const float NULL_BAND_G          = 0.5f;     // unter +-0,5 g wird 0.0 angezeigt
const float FILTER_ALPHA         = 0.3f;     // Glaettung (0..1, kleiner = ruhiger)
const float JUMP_G               = 20.0f;    // grosse Aenderung -> sofort folgen
const unsigned long AUTO_OFF_MS  = 180000UL; // 3 Minuten ohne Aenderung -> aus
const float AUTO_OFF_CHANGE_G    = 2.0f;     // ab dieser Aenderung gilt "Aktivitaet"
const unsigned long DISPLAY_MS   = 200;      // Display-Aktualisierung
const unsigned long BATTERY_MS   = 5000;     // Batteriemessung

// Spannungsteiler: (R1 + R2) / R2 = (1000k + 100k) / 100k = 11
const long DIVIDER_RATIO         = 11;
const uint16_t DEFAULT_VREF_MV   = 1100;     // interne Referenz (ca. +-10 %)
const int LOW_BATTERY_MV         = 6500;     // Warnschwelle 9-V-Block

const int EEPROM_ADDR_MAGIC   = 0;
const int EEPROM_ADDR_FACTOR  = 2;
const int EEPROM_ADDR_VREF    = 6;
const uint16_t EEPROM_MAGIC   = 0xBEEF;

// ---------------- Objekte ----------------
// _1_ = kleiner Seitenpuffer, spart RAM
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
HX711 scale;

// ---------------- Variablen ----------------
float calibrationFactor = DEFAULT_FACTOR;
uint16_t vrefMv = DEFAULT_VREF_MV;

float filteredWeight = 0.0f;
bool filterReady = false;

float activityRefWeight = 0.0f;
unsigned long lastActivityMs = 0;
unsigned long lastHxReadyMs  = 0;
unsigned long lastDisplayMs  = 0;
unsigned long lastBatteryMs  = 0;
int batteryMv = 0;

// Taster (Index 0 = Tara, 1 = Aus)
const byte BTN_COUNT = 2;
const byte btnPins[BTN_COUNT] = { PIN_TARA, PIN_AUS };
bool btnStable[BTN_COUNT] = { HIGH, HIGH };
bool btnLastRead[BTN_COUNT] = { HIGH, HIGH };
unsigned long btnChangedMs[BTN_COUNT] = { 0, 0 };


// ---------------- Taster ----------------
// Liefert genau einmal true, wenn der Taster gedrueckt wird (entprellt).
bool buttonPressed(byte i) {
  bool reading = digitalRead(btnPins[i]);
  unsigned long now = millis();

  if (reading != btnLastRead[i]) {
    btnLastRead[i] = reading;
    btnChangedMs[i] = now;
  }

  if ((now - btnChangedMs[i]) > 30 && reading != btnStable[i]) {
    btnStable[i] = reading;
    if (reading == LOW) {
      return true;
    }
  }
  return false;
}

void waitButtonsReleased() {
  delay(30);
  while (digitalRead(PIN_TARA) == LOW || digitalRead(PIN_AUS) == LOW) {
    delay(10);
  }
  delay(30);
}

void resetButtons() {
  for (byte i = 0; i < BTN_COUNT; i++) {
    btnStable[i] = HIGH;
    btnLastRead[i] = HIGH;
    btnChangedMs[i] = millis();
  }
}


// ---------------- EEPROM ----------------
void loadSettings() {
  uint16_t magic;
  float f;
  uint16_t v;

  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  EEPROM.get(EEPROM_ADDR_FACTOR, f);
  EEPROM.get(EEPROM_ADDR_VREF, v);

  if (magic != EEPROM_MAGIC) {
    calibrationFactor = DEFAULT_FACTOR;
    vrefMv = DEFAULT_VREF_MV;
    return;
  }

  if (isnan(f) || f == 0.0f) {
    calibrationFactor = DEFAULT_FACTOR;
  } else {
    calibrationFactor = f;
  }

  if (v < 900 || v > 1300) {
    vrefMv = DEFAULT_VREF_MV;
  } else {
    vrefMv = v;
  }
}

void saveSettings() {
  EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR_FACTOR, calibrationFactor);
  EEPROM.put(EEPROM_ADDR_VREF, vrefMv);
}


// ---------------- Batterie ----------------
// Erste Messungen nach Umschalten der Referenz verwerfen.
void settleAdc() {
  for (byte i = 0; i < 5; i++) {
    analogRead(PIN_BAT);
    delay(2);
  }
}

// Misst die 9-V-Batterie ueber den Spannungsteiler an A0.
int readBatteryMv() {
  long sum = 0;
  for (byte i = 0; i < 8; i++) {
    sum += analogRead(PIN_BAT);
    delay(1);
  }
  long adc = sum / 8;
  return (int)(adc * (long)vrefMv * DIVIDER_RATIO / 1023L);
}


// ---------------- Display ----------------
void showMessage(const char* line1, const char* line2 = "") {
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(0, 24, line1);
    oled.drawStr(0, 44, line2);
  } while (oled.nextPage());
}

// Smiley zeichnen
// r = Groesse des Gesichts, details = Augen/Mund/Text, wink = rechtes Auge zu
void drawSmiley(byte r, bool details, bool wink) {
  oled.firstPage();
  do {
    // Gesicht (doppelte Linie, damit es kraeftiger aussieht)
    oled.drawCircle(64, 30, r);
    if (r > 1) {
      oled.drawCircle(64, 30, r - 1);
    }

    if (details) {
      // Linkes Auge
      oled.drawDisc(56, 24, 3);

      // Rechtes Auge: offen oder zwinkernd
      if (wink) {
        oled.drawHLine(69, 24, 7);
        oled.drawHLine(69, 25, 7);
      } else {
        oled.drawDisc(72, 24, 3);
      }

      // Laechelnder Mund (untere Haelfte eines Kreises)
      oled.drawCircle(64, 32, 12, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
      oled.drawCircle(64, 32, 11, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);

      // Text
      oled.setFont(u8g2_font_6x12_tf);
      oled.drawStr(46, 63, "Hallo!");
    }
  } while (oled.nextPage());
}

// Kleine Boot-Animation
void showBootSmiley() {
  // Gesicht waechst
  for (byte r = 4; r <= 22; r += 3) {
    drawSmiley(r, false, false);
    delay(30);
  }

  // Laecheln
  drawSmiley(22, true, false);
  delay(700);

  // Zwinkern
  drawSmiley(22, true, true);
  delay(300);

  // Wieder offen
  drawSmiley(22, true, false);
  delay(600);
}

void showWeight(float weight) {
  if (fabs(weight) < NULL_BAND_G) {
    weight = 0.0f;
  }

  char num[16];
  dtostrf(weight, 1, 1, num);

  char bat[10];
  snprintf(bat, sizeof(bat), "%d.%dV",
           batteryMv / 1000, (batteryMv % 1000) / 100);

  oled.firstPage();
  do {
    // Kopfzeile
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(0, 10, "Waage");
    if (batteryMv > 0 && batteryMv < LOW_BATTERY_MV) {
      oled.drawStr(44, 10, "LOW");
    }
    oled.drawStr(128 - oled.getStrWidth(bat), 10, bat);

    // Gewicht rechtsbuendig
    oled.setFont(u8g2_font_logisoso24_tn);
    int x = 110 - oled.getStrWidth(num);
    if (x < 0) {
      x = 0;
    }
    oled.drawStr(x, 50, num);

    // Einheit
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(115, 50, "g");
  } while (oled.nextPage());
}


// ---------------- Waage ----------------
void doTare() {
  showMessage("Tariere...", "Nicht beruehren");

  if (scale.wait_ready_timeout(1000)) {
    scale.tare(10);
    filterReady = false;
    showMessage("Tara gesetzt");
    Serial.println(F("Tara gesetzt."));
  } else {
    showMessage("HX711 Fehler", "Anschluss pruefen");
    Serial.println(F("HX711 antwortet nicht."));
  }

  delay(400);
  lastActivityMs = millis();
  lastHxReadyMs = millis();
}

void readScale() {
  if (!scale.is_ready()) {
    return;
  }

  float raw = scale.get_units(1);
  lastHxReadyMs = millis();

  if (!filterReady) {
    filteredWeight = raw;
    activityRefWeight = raw;
    filterReady = true;
  } else if (fabs(raw - filteredWeight) > JUMP_G) {
    filteredWeight = raw;   // grosse Aenderung: sofort uebernehmen
  } else {
    filteredWeight += FILTER_ALPHA * (raw - filteredWeight);
  }

  // Aktivitaet fuer Auto-Aus erkennen
  if (fabs(filteredWeight - activityRefWeight) > AUTO_OFF_CHANGE_G) {
    activityRefWeight = filteredWeight;
    lastActivityMs = millis();
  }
}


// ---------------- Serielle Befehle ----------------
void handleSerial() {
  if (!Serial.available()) {
    return;
  }

  char c = Serial.read();

  if (c == 't' || c == 'T') {
    doTare();
  }
  else if (c == 'k' || c == 'K') {
    float grams = Serial.parseFloat();
    if (grams <= 0.0f) {
      Serial.println(F("Beispiel: k500  (fuer 500 g Referenzgewicht)"));
      return;
    }

    showMessage("Kalibriere...", "Nicht beruehren");
    Serial.println(F("Kalibriere..."));

    if (!scale.wait_ready_timeout(1000)) {
      Serial.println(F("HX711 antwortet nicht."));
      return;
    }

    float value = scale.get_value(15);
    float newFactor = value / grams;

    if (newFactor == 0.0f || isnan(newFactor)) {
      Serial.println(F("Fehler: Erst ohne Gewicht tarieren (t), dann Gewicht auflegen."));
      return;
    }

    calibrationFactor = newFactor;
    scale.set_scale(calibrationFactor);
    saveSettings();
    filterReady = false;

    Serial.print(F("Neuer Faktor gespeichert: "));
    Serial.println(calibrationFactor, 4);
    showMessage("Kalibriert", "Faktor gespeichert");
    delay(800);
    lastActivityMs = millis();
  }
  else if (c == 'b' || c == 'B') {
    float volts = Serial.parseFloat();
    if (volts < 3.0f || volts > 12.0f) {
      Serial.println(F("Beispiel: b9.12  (mit Multimeter gemessene Batteriespannung)"));
      return;
    }

    int shownMv = readBatteryMv();
    if (shownMv <= 0) {
      Serial.println(F("Fehler: Keine Spannung an A0. Spannungsteiler pruefen."));
      return;
    }

    long newVref = (long)vrefMv * (long)(volts * 1000.0f) / shownMv;
    if (newVref < 900 || newVref > 1300) {
      Serial.println(F("Fehler: Wert unplausibel. Spannungsteiler pruefen."));
      return;
    }

    vrefMv = (uint16_t)newVref;
    saveSettings();
    batteryMv = readBatteryMv();

    Serial.print(F("Batterieabgleich gespeichert. Vref = "));
    Serial.print(vrefMv);
    Serial.println(F(" mV"));
  }
  else if (c == 'f' || c == 'F') {
    Serial.print(F("Waagenfaktor: "));
    Serial.println(calibrationFactor, 4);
    Serial.print(F("Vref: "));
    Serial.print(vrefMv);
    Serial.println(F(" mV"));
    Serial.print(F("Batterie: "));
    Serial.print(readBatteryMv());
    Serial.println(F(" mV"));
  }
}


// ---------------- Schlafmodus ----------------
void wakeISR() {
  // Sofort abmelden, sonst feuert der LOW-Interrupt dauernd.
  detachInterrupt(digitalPinToInterrupt(PIN_TARA));
}

void goToSleep() {
  showMessage("Aus");
  delay(400);
  waitButtonsReleased();

  Serial.println(F("Schlafmodus."));
  Serial.flush();

  scale.power_down();
  oled.setPowerSave(1);

  byte oldAdcsra = ADCSRA;
  ADCSRA = 0;   // ADC aus

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  noInterrupts();
  sleep_enable();
  attachInterrupt(digitalPinToInterrupt(PIN_TARA), wakeISR, LOW);
  sleep_bod_disable();
  interrupts();
  sleep_cpu();

  // ---- Hier geht es nach dem Aufwecken weiter ----
  sleep_disable();
  ADCSRA = oldAdcsra;
  settleAdc();

  oled.setPowerSave(0);
  scale.power_up();

  waitButtonsReleased();
  resetButtons();

  showBootSmiley();    // Smiley auch beim Aufwecken
  showMessage("Waage startet...");

  batteryMv = readBatteryMv();
  lastBatteryMs = millis();

  doTare();            // "An" = einschalten + tarieren
}


// ---------------- Setup / Loop ----------------
void setup() {
  pinMode(PIN_TARA, INPUT_PULLUP);
  pinMode(PIN_AUS, INPUT_PULLUP);

  Serial.begin(9600);

  // Interne 1,1-V-Referenz: unabhaengig von der Reglerspannung
  analogReference(INTERNAL);
  settleAdc();

  oled.setBusClock(400000);
  oled.begin();

  showBootSmiley();    // Smiley beim Einschalten
  showMessage("Waage startet...");

  loadSettings();

  scale.begin(PIN_HX_DOUT, PIN_HX_SCK);
  scale.set_scale(calibrationFactor);

  batteryMv = readBatteryMv();
  lastBatteryMs = millis();

  Serial.println(F("Waage bereit."));
  Serial.println(F("Befehle: t = Tara, k500 = Kalibrieren, b9.12 = Batterieabgleich, f = Werte"));
  Serial.print(F("Batterie: "));
  Serial.print(batteryMv);
  Serial.println(F(" mV"));

  delay(500);
  doTare();
  resetButtons();
}

void loop() {
  handleSerial();

  // Aus-Taster
  if (buttonPressed(1)) {
    goToSleep();
    return;
  }

  // Tara-Taster
  if (buttonPressed(0)) {
    doTare();
  }

  readScale();

  unsigned long now = millis();

  // Batterie gelegentlich messen
  if (now - lastBatteryMs >= BATTERY_MS) {
    batteryMv = readBatteryMv();
    lastBatteryMs = now;
  }

  // Display aktualisieren
  if (now - lastDisplayMs >= DISPLAY_MS) {
    lastDisplayMs = now;

    if (now - lastHxReadyMs > 1500) {
      showMessage("HX711 Fehler", "Anschluss pruefen");
    } else if (filterReady) {
      showWeight(filteredWeight);
    }
  }

  // Automatisch ausschalten
  if (now - lastActivityMs > AUTO_OFF_MS) {
    goToSleep();
  }
}
