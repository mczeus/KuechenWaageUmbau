# Digitale Waage mit Arduino Pro Mini, HX711 und OLED

Eine batteriebetriebene Digitalwaage mit einem **Arduino Pro Mini (ATmega328P, 3,3 V / 8 MHz)**, einem **HX711-Wägezellenverstärker** und einem **0,96″-OLED-Display (SSD1306, I²C)**. Bedient wird sie mit zwei Tastern. Kalibrierwerte werden im EEPROM gespeichert, die Batteriespannung wird angezeigt, und beim Einschalten begrüßt dich ein zwinkernder Smiley. 😉

---

## Funktionen

- Gewichtsanzeige in Gramm mit einer Nachkommastelle
- Glättungsfilter für eine ruhige Anzeige, der großen Gewichtsänderungen sofort folgt
- Nullpunkt-Unterdrückung: Werte unter ±0,5 g werden als `0.0` angezeigt
- **An/Tara-Taster:** Aufwecken aus dem Schlafmodus mit automatischer Tara, im Betrieb neu tarieren
- **Aus-Taster:** Stromsparender Schlafmodus (Display, HX711, ADC und Brown-out-Erkennung aus)
- **Auto-Aus** nach 3 Minuten ohne Gewichtsänderung
- **Batterieanzeige** für den 9-V-Block über einen Spannungsteiler und die interne 1,1-V-Referenz, mit `LOW`-Warnung
- **Kalibrierung über den seriellen Monitor**, dauerhaft im EEPROM gespeichert
- Fehleranzeige, wenn der HX711 nicht antwortet
- Speichersparender Display-Seitenpuffer für die 2 KB RAM des ATmega328P
- Boot-Animation mit Smiley

---

## Hardware

| Bauteil | Hinweis |
|---|---|
| Arduino Pro Mini (ATmega328P) | 3,3-V-/8-MHz-Variante |
| HX711-Modul | muss mit 3–3,6 V laufen |
| Wägezelle | z. B. 1 kg / 5 kg Biegebalken |
| 0,96″-OLED, 128×64, SSD1306, I²C | 3,3-V-tauglich |
| 2 Taster | Schließer |
| 9-V-Block | Versorgung |
| Spannungsregler 3,0–3,6 V (z. B. HT71xx) | 9 V → VCC |
| Widerstand 1 MΩ | Spannungsteiler (R1) |
| Widerstand 100 kΩ | Spannungsteiler (R2) |
| Kondensator 1–100 nF | an A0, 100 nF empfohlen |
| USB-Seriell-Adapter (FTDI o. ä.) | zum Programmieren und Kalibrieren |

---

## Verdrahtung

| Bauteil | Anschluss | Pro Mini |
|---|---|---|
| HX711 | DOUT | D4 |
| HX711 | SCK | D5 |
| HX711 | VCC / GND | VCC / GND |
| OLED | SDA | A4 |
| OLED | SCL | A5 |
| OLED | VCC / GND | VCC / GND |
| Taster An/Tara | eine Seite | D2 (andere Seite an GND) |
| Taster Aus | eine Seite | D3 (andere Seite an GND) |
| Spannungsteiler | Mittelpunkt | A0 |
| Spannungsregler | Ausgang | VCC (**nicht** RAW) |

Die Wägezelle wird am HX711 an `E+`, `E−`, `A+` und `A−` angeschlossen. Die Kabelfarben sind nicht bei allen Herstellern gleich.

Die Taster brauchen keine externen Widerstände, der Sketch nutzt die internen Pull-ups. **D2 muss für den An/Tara-Taster verwendet werden**, weil nur dieser Pin (INT0) den Chip aus dem Tiefschlaf weckt.

### Stromversorgung und Batteriemessung

```
9V-Batterie (+) ──┬──────────────► Eingang Spannungsregler ──► VCC Pro Mini
                  │
                [R1] 1 MΩ
                  │
                  ├──────────────► A0
                  │       │
                [R2]    [C] 1–100 nF
                100 kΩ    │
                  │       │
GND ──────────────┴───────┴──────► GND (gemeinsam)
```

> ⚠️ **Niemals 9 V direkt an A0 anschließen!** Der Teiler teilt die Spannung durch 11. Messbar sind bis etwa 12 V.

---

## Software

### Benötigte Bibliotheken

Installation in der Arduino-IDE über **Werkzeuge → Bibliotheken verwalten**:

- **HX711** von Bogdan Necula
- **U8g2** von oliver

`Wire`, `EEPROM` und `avr/sleep` sind bereits in der Arduino-Umgebung enthalten.

### Board-Einstellungen

- **Board:** Arduino Pro or Pro Mini
- **Prozessor:** ATmega328P (3.3V, 8 MHz)

### Hochladen

1. Repository klonen oder herunterladen.
2. `Waage/Waage.ino` in der Arduino-IDE öffnen. Der Ordner muss genauso heißen wie die `.ino`-Datei.
3. USB-Seriell-Adapter anschließen und hochladen.

> Hinweis: Die Meldung „Wenig Arbeitsspeicher verfügbar“ sollte mit dieser Version nicht mehr erscheinen, da das Display einen kleinen Seitenpuffer (`_1_`) statt eines Vollbildpuffers verwendet.

---

## Bedienung

| Aktion | Wirkung |
|---|---|
| **An/Tara** drücken, während die Waage aus ist | Einschalten, Smiley, automatische Tara |
| **An/Tara** drücken im Betrieb | Neu tarieren |
| **Aus** drücken | Schlafmodus |
| 3 Minuten keine Gewichtsänderung | Automatisch in den Schlafmodus |

Beim Tarieren die Waage nicht berühren.

---

## Kalibrierung

Den seriellen Monitor mit **9600 Baud** öffnen.

### Waage kalibrieren

1. Waage leer lassen und `t` senden.
2. Ein bekanntes Gewicht auflegen, z. B. 500 g.
3. `k500` senden (Zahl = Gewicht in Gramm).

Der Faktor wird im EEPROM gespeichert und bleibt auch nach erneutem Hochladen erhalten.

Wird das Gewicht negativ angezeigt, `A+` und `A−` an der Wägezelle tauschen und neu kalibrieren.

### Batterieanzeige abgleichen

Die interne Referenz ist ab Werk nur auf etwa ±10 % genau.

1. Batteriespannung mit einem Multimeter messen, z. B. 9,12 V.
2. `b9.12` senden.

### Serielle Befehle

| Befehl | Funktion |
|---|---|
| `t` | Tarieren |
| `k500` | Kalibrieren mit 500 g Referenzgewicht |
| `b9.12` | Batterieanzeige auf 9,12 V abgleichen |
| `f` | Aktuelle Werte anzeigen (Faktor, Vref, Batterie) |

---

## Anpassungen

Alle wichtigen Einstellungen stehen oben im Sketch:

| Konstante | Standard | Bedeutung |
|---|---|---|
| `AUTO_OFF_MS` | `180000` | Zeit bis zum Auto-Aus in ms |
| `NULL_BAND_G` | `0.5` | Werte darunter werden als 0 angezeigt |
| `FILTER_ALPHA` | `0.3` | Glättung (kleiner = ruhiger, aber träger) |
| `JUMP_G` | `20.0` | Ab dieser Änderung wird sofort umgeschaltet |
| `LOW_BATTERY_MV` | `6500` | Schwelle für die `LOW`-Warnung |
| `DIVIDER_RATIO` | `11` | (R1 + R2) / R2 des Spannungsteilers |

- **Smiley-Text:** In `drawSmiley()` die Zeile `oled.drawStr(46, 63, "Hallo!");` ändern. Die Schrift kennt keine Umlaute, deshalb `ae`, `oe`, `ue` verwenden.
- **Smiley nur beim ersten Start:** In `goToSleep()` die Zeile `showBootSmiley();` entfernen.

---

## Tipps zum Stromverbrauch

- Die **Power-LED** auf dem Pro Mini verbraucht dauerhaft etwa 1–3 mA, auch im Schlafmodus. Für lange Batterielaufzeit die LED oder ihren Vorwiderstand ablöten. Das gilt auch für LEDs auf dem HX711- oder OLED-Modul.
- Der Spannungsteiler verbraucht nur etwa 8 µA.
- Ein 9-V-Block hat nur etwa 500 mAh. Mit Power-LED ist er auch im Schlafmodus nach einigen Wochen leer.
- Der ATmega328P mit 8 MHz braucht mindestens etwa 2,7 V. Der Spannungsregler sollte also mindestens 3,0 V liefern.

---

## Fehlersuche

| Problem | Mögliche Ursache |
|---|---|
| „HX711 Fehler – Anschluss pruefen“ | DOUT/SCK vertauscht oder Versorgung des HX711 fehlt |
| Display bleibt dunkel | SDA/SCL vertauscht, falsche I²C-Adresse oder kein SSD1306-Controller (z. B. SH1106) |
| Gewicht negativ | `A+` und `A−` vertauschen und neu kalibrieren |
| Anzeige schwankt stark | Wägezelle mechanisch nicht fest montiert, lange ungeschirmte Kabel |
| Batterieanzeige springt | Größeren Kondensator an A0 verwenden (10–100 nF) |
| Batterieanzeige zeigt 0 V | Spannungsteiler nicht angeschlossen |
| Waage wacht nicht auf | An/Tara-Taster nicht an D2 angeschlossen |

**Bei einem SH1106-Display** (oft bei 1,3″-Modulen) im Sketch diese Zeile ersetzen:

```cpp
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
```

durch:

```cpp
U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
```

---

## Projektstruktur

```
.
├── README.md
└── Waage/
    └── Waage.ino
```

---

## Lizenz

Bitte vor der Veröffentlichung eine `LICENSE`-Datei hinzufügen, z. B. die MIT-Lizenz. GitHub bietet beim Anlegen des Repositorys eine Auswahl an.
