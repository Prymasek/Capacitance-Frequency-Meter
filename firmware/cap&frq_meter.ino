#include <Arduino.h>
#include <LiquidCrystal.h>

// --- Konfiguracja PINów ---
const int rs = 21, en = 20, d4 = 19, d5 = 18, d6 = 10, d7 = 9;
const int chargePin = 4;
const int compPinC = 3;
const int freqInputPin = 6;
const int btnModePin = 8;
const int btnTarePin = 7;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// --- Zmienne globalne ---
enum Mode { MODE_CAPACITANCE, MODE_FREQUENCY };
Mode currentMode = MODE_CAPACITANCE;

bool isPaused = false;
float offset_nF = 0.0;
const float R = 100000.0; // 100k Ohm
unsigned long lastCapMeasureTime = 0; // Czas ostatniego pomiaru C

// Zmienne dla pojemności (C)
volatile uint32_t t_start = 0;
volatile uint32_t t_stop = 0;
volatile bool doneC = false;

// Zmienne dla częstotliwości (F)
volatile unsigned long pulseCount = 0;
bool lastFreqState = false;
unsigned long lastPulseTime = 0;
unsigned long currentPeriod = 0;
unsigned long lastReportTime = 0;

// --- Przerwania ---
void IRAM_ATTR onComparatorMatch() {
  t_stop = micros();
  digitalWrite(chargePin, LOW);
  doneC = true;
}

void setup() {
  lcd.begin(16, 2);
  lcd.print("Miernik C i f");
  lcd.setCursor(0, 1);
  lcd.print("V2.0 - Rozszerz.");
  delay(2000);

  pinMode(chargePin, OUTPUT);
  digitalWrite(chargePin, LOW);
  pinMode(compPinC, INPUT);
  pinMode(freqInputPin, INPUT);
  pinMode(btnModePin, INPUT_PULLUP);
  pinMode(btnTarePin, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(compPinC), onComparatorMatch, RISING);
  lcd.clear();
}

void showTransition(const char* text) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TRYB:");
  lcd.setCursor(0, 1);
  lcd.print(text);
  delay(1500);
  lcd.clear();
}

// --- Funkcja formatująca jednostki C ---
void displayCapacitance(float nF) {
  lcd.setCursor(0, 0);
  lcd.print("C: ");
  if (nF < 1.0) {
    lcd.print(nF * 1000.0, 1);
    lcd.print(" pF     ");
  } else if (nF >= 1000.0) {
    lcd.print(nF / 1000.0, 3);
    lcd.print(" uF     ");
  } else {
    lcd.print(nF, 3);
    lcd.print(" nF     ");
  }
}

void handleCapacitance() {
  unsigned long currentMillis = millis();

  // Obsługa tary (wymusza natychmiastowy pomiar)
  if (digitalRead(btnTarePin) == LOW) {
    doneC = false;
    t_start = micros();
    digitalWrite(chargePin, HIGH);
    uint32_t t0 = millis();
    while (!doneC && (millis() - t0 < 1000)) yield();
    if (doneC) {
      offset_nF = ((float)(t_stop - t_start) / R) * 1000.0;
      lcd.setCursor(0, 1);
      lcd.print("TARA: OK!      ");
      delay(800);
    }
    lastCapMeasureTime = currentMillis; // Resetuj timer po tarowaniu
  }

  // Pomiar co 10 sekund (lub jeśli to pierwszy pomiar)
  if (currentMillis - lastCapMeasureTime >= 10000 || lastCapMeasureTime == 0) {
    doneC = false;
    t_start = micros();
    digitalWrite(chargePin, HIGH);

    uint32_t startTimeCheck = millis();
    while (!doneC && (millis() - startTimeCheck < 3000)) {
      if (digitalRead(btnModePin) == LOW) return; 
      yield();
    }

    if (doneC) {
      float deltaT = (float)(t_stop - t_start);
      float raw_nF = (deltaT / R) * 1000.0;
      float capacitance_nF = raw_nF - offset_nF;
      if (capacitance_nF < 0) capacitance_nF = 0;

      displayCapacitance(capacitance_nF);
      lcd.setCursor(0, 1);
      lcd.print("T: "); lcd.print((int)deltaT); lcd.print(" us    ");
    } else {
      digitalWrite(chargePin, LOW);
      lcd.setCursor(0, 0);
      lcd.print("Brak kondensat.");
      lcd.setCursor(0, 1);
      lcd.print("Timeout / Open ");
    }
    lastCapMeasureTime = currentMillis;
  } else {
    // Odliczanie do następnego pomiaru na LCD (opcjonalne)
    lcd.setCursor(13, 0);
    lcd.print((10000 - (currentMillis - lastCapMeasureTime)) / 1000);
    lcd.print("s ");
  }
}

void handleFrequency() {
  unsigned long now = millis();
  unsigned long nowMicros = micros();

  static bool lastTareBtnState = HIGH;
  bool currentTareBtnState = digitalRead(btnTarePin);
  if (currentTareBtnState == LOW && lastTareBtnState == HIGH) {
    isPaused = !isPaused;
    delay(50);
  }
  lastTareBtnState = currentTareBtnState;

  if (isPaused) {
    lcd.setCursor(13, 0);
    lcd.print("[H]");
    return; 
  }

  bool currentState = digitalRead(freqInputPin);
  if (currentState == HIGH && lastFreqState == LOW) {
    pulseCount++;
    currentPeriod = nowMicros - lastPulseTime;
    lastPulseTime = nowMicros;
  }
  lastFreqState = currentState;

  if (now - lastReportTime >= 1000) {
    float frequency = (float)pulseCount;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("f: "); 
    if (frequency >= 1000.0) {
      lcd.print(frequency / 1000.0, 3); lcd.print(" kHz");
    } else {
      lcd.print(frequency, 1); lcd.print(" Hz");
    }

    lcd.setCursor(0, 1);
    if (pulseCount > 0) {
      float periodMs = currentPeriod / 1000.0;
      if (periodMs < 1.0) {
        lcd.print("T: "); lcd.print(periodMs * 1000.0, 1); lcd.print(" us");
      } else {
        lcd.print("T: "); lcd.print(periodMs, 2); lcd.print(" ms");
      }
    } else {
      lcd.print("Brak sygnalu");
    }

    pulseCount = 0;
    lastReportTime = now;
  }
}

void loop() {
  if (digitalRead(btnModePin) == LOW) {
    delay(50); 
    if (currentMode == MODE_CAPACITANCE) {
      currentMode = MODE_FREQUENCY;
      showTransition("CZESTOTLIWOSC");
    } else {
      currentMode = MODE_CAPACITANCE;
      showTransition("POJEMNOSC");
      lastCapMeasureTime = 0; // Wymuś natychmiastowy pomiar po powrocie
    }
    while(digitalRead(btnModePin) == LOW); 
  }

  if (currentMode == MODE_CAPACITANCE) {
    handleCapacitance();
  } else {
    handleFrequency();
  }
}