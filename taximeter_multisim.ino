/*
 Taxi meter (Multisim-compatible wiring mapping)
 - 4-digit 7-seg display, format XX.YY representing 0.00 .. 99.99 (cents integer 0..9999)
 - Fare composed of: start fee, distance fee (per full km), waiting fee (per full 10 min)
 - Distance pulses from wheel/hall sensor -> counted via interrupt
 - Only whole kilometers increment billing (partial km ignored)
 - Waiting detected when no pulses for IDLE_MS; waiting billed per 10 minutes (partial ignored)
 - Settings: enter with MODE button, adjust with UP/DOWN, save to EEPROM

 Pin mapping (matches the Multisim wiring instructions in MULTISIM_INSTRUCTIONS.md):
 - PULSE_PIN = 2  // INT0
 - SEG_A..SEG_G, SEG_DP = 3..10
 - DIG1..DIG4 bits = 11,12,13,A0
 - Buttons: MODE=A2, UP=A3, DOWN=A4, STARTSTOP=A5, RESET=A6
*/

#include <EEPROM.h>

// ----------------- Pins (change as needed) -----------------
const uint8_t PULSE_PIN = 2;      // external interrupt pin (INT0)
const uint8_t BTN_MODE = A2;
const uint8_t BTN_UP   = A3;
const uint8_t BTN_DOWN = A4;
const uint8_t BTN_STARTSTOP = A5;
const uint8_t BTN_RESET = A6;

// Segment pins for A,B,C,D,E,F,G,DP (map to your wiring)
const uint8_t SEG_A = 3;
const uint8_t SEG_B = 4;
const uint8_t SEG_C = 5;
const uint8_t SEG_D = 6;
const uint8_t SEG_E = 7;
const uint8_t SEG_F = 8;
const uint8_t SEG_G = 9;
const uint8_t SEG_DP= 10;

// Digit common pins (from left digit1 to digit4)
const uint8_t DIG1 = 11;
const uint8_t DIG2 = 12;
const uint8_t DIG3 = 13;
const uint8_t DIG4 = A0;

// ----------------- Parameters (can be changed / saved to EEPROM) -----------------
// Physical sensor / wheel parameters
const float WHEEL_DIAMETER_M = 0.6;   // m
const float PULSES_PER_REV = 1.0;     // pulses per wheel revolution

// Derived (computed in setup)
unsigned long PULSES_PER_KM = 0; // integer pulses corresponding to 1000 m

// IDLE detection and waiting threshold
const unsigned long IDLE_MS = 5000UL;              // no pulses for this -> considered waiting
const unsigned long WAIT_PERIOD_MS = 10UL * 60UL * 1000UL; // 10 minutes threshold for billing

// Billing settings stored in cents
uint16_t startFee_cents = 1000;    // default 10.00 元 => 1000 cents
uint16_t pricePerKm_cents = 250;   // default 2.50 元/km => 250 cents
uint16_t waitPer10min_cents = 50;   // default 0.50 元 per 10 min => 50 cents

// EEPROM addresses
const int EE_ADDR_START = 0;

// Fare state
volatile unsigned long pulseCount = 0;   // raw pulses since last km-handling
volatile unsigned long totalPulses = 0;  // cumulative pulses since ride start (not required but useful)
unsigned long lastPulseMillis = 0;
bool running = false;
unsigned long rideStartMillis = 0;

// Charged amounts (cents)
unsigned int fare_cents = 0; // includes start fee once at start
const unsigned int MAX_FARE_CENTS = 9999; // 99.99 max

// waiting accumulation
unsigned long waitingAccumMs = 0;
bool inWaitingState = false;

// display buffer
uint8_t dispDigits[4] = {0,0,0,0};
bool dispDP[4] = {false,false,false,false};

// multiplex timing
unsigned long lastRefresh = 0;
const unsigned int REFRESH_MS = 2; // each digit on ~2ms

// digit segment map (segments A..G => bit 0..6), DP handled separately
const uint8_t digitSegs[10] = {
  // gfedcba (we map to pins manually)
  // a b c d e f g  (we'll write bits in this order)
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

// helper to write a segment bits to pins
void writeSegments(uint8_t segBits, bool dp) {
  digitalWrite(SEG_A, segBits & 0x01);
  digitalWrite(SEG_B, (segBits>>1) & 0x01);
  digitalWrite(SEG_C, (segBits>>2) & 0x01);
  digitalWrite(SEG_D, (segBits>>3) & 0x01);
  digitalWrite(SEG_E, (segBits>>4) & 0x01);
  digitalWrite(SEG_F, (segBits>>5) & 0x01);
  digitalWrite(SEG_G, (segBits>>6) & 0x01);
  digitalWrite(SEG_DP, dp ? HIGH : LOW);
}

// activate only one digit at a time (common-anode vs common-cathode adjustments may be needed)
void activateDigit(uint8_t idx) {
  // turn all off first
  digitalWrite(DIG1, LOW);
  digitalWrite(DIG2, LOW);
  digitalWrite(DIG3, LOW);
  digitalWrite(DIG4, LOW);
  // then enable the selected; assume common cathode (set HIGH to enable)
  uint8_t pins[4] = {DIG1, DIG2, DIG3, DIG4};
  digitalWrite(pins[idx], HIGH);
}

// deactivate all digits
void deactivateAllDigits() {
  digitalWrite(DIG1, LOW);
  digitalWrite(DIG2, LOW);
  digitalWrite(DIG3, LOW);
  digitalWrite(DIG4, LOW);
}

// ISR for pulse count
void IRAM_ATTR pulseISR() {
  pulseCount++;
  totalPulses++;
  lastPulseMillis = millis();
}

// format fare_cents into dispDigits and DP: format XX.YY
void formatDisplayFromFare() {
  unsigned int v = fare_cents;
  if (v > MAX_FARE_CENTS) v = MAX_FARE_CENTS;
  // hundreds (cents) => integer part = v/100, decimal cents = v%100 (two digits)
  unsigned int intPart = v / 100; // 0..99
  unsigned int decPart = v % 100; // 0..99
  dispDigits[0] = intPart / 10;  // tens of integer
  dispDigits[1] = intPart % 10;  // units of integer
  dispDigits[2] = decPart / 10;  // tenths (jiao)
  dispDigits[3] = decPart % 10;  // cents
  // DP between digit 1 and 2 (i.e., after second digit from left)
  dispDP[0] = false;
  dispDP[1] = true; // show decimal point after DIG2
  dispDP[2] = false;
  dispDP[3] = false;
}

// multiplex refresh: call frequently in loop
void refreshDisplay() {
  unsigned long t = millis();
  if (t - lastRefresh < REFRESH_MS) return;
  lastRefresh = t;
  static uint8_t idx = 0;
  deactivateAllDigits();
  uint8_t segBits = digitSegs[ dispDigits[idx] ];
  writeSegments(segBits, dispDP[idx]);
  activateDigit(idx);
  idx = (idx + 1) & 0x03;
}

// load/save settings to EEPROM
void saveSettingsToEEPROM() {
  EEPROM.put(EE_ADDR_START, startFee_cents);
  EEPROM.put(EE_ADDR_START + 2, pricePerKm_cents);
  EEPROM.put(EE_ADDR_START + 4, waitPer10min_cents);
}

void loadSettingsFromEEPROM() {
  uint16_t a,b,c;
  EEPROM.get(EE_ADDR_START, a);
  EEPROM.get(EE_ADDR_START + 2, b);
  EEPROM.get(EE_ADDR_START + 4, c);
  // basic validation
  if (a > 0 && a <= 9999) startFee_cents = a;
  if (b > 0 && b <= 9999) pricePerKm_cents = b;
  if (c <= 9999) waitPer10min_cents = c;
}

// simple button read with debounce
bool readButton(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

void setupPins() {
  // segments
  pinMode(SEG_A, OUTPUT); pinMode(SEG_B, OUTPUT);
  pinMode(SEG_C, OUTPUT); pinMode(SEG_D, OUTPUT);
  pinMode(SEG_E, OUTPUT); pinMode(SEG_F, OUTPUT);
  pinMode(SEG_G, OUTPUT); pinMode(SEG_DP, OUTPUT);
  // digits
  pinMode(DIG1, OUTPUT); pinMode(DIG2, OUTPUT);
  pinMode(DIG3, OUTPUT); pinMode(DIG4, OUTPUT);
  deactivateAllDigits();
  // buttons
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_STARTSTOP, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  // pulse input
  pinMode(PULSE_PIN, INPUT_PULLUP);
}

void applyStartRide() {
  running = true;
  rideStartMillis = millis();
  fare_cents = startFee_cents; // apply start fee immediately
  pulseCount = 0;
  totalPulses = 0;
  waitingAccumMs = 0;
  inWaitingState = false;
  lastPulseMillis = millis();
  formatDisplayFromFare();
}

void applyStopRide() {
  running = false;
  // per requirements: partial km or waiting less than threshold ignored (we do nothing)
  // fare_cents remains as last updated
  formatDisplayFromFare();
}

// ---------- setup & main loop ----------
void setup() {
  setupPins();
  loadSettingsFromEEPROM();
  // compute pulses per km
  float circumference = 3.14159265358979323846 * WHEEL_DIAMETER_M; // m per rev
  float d_pulse = circumference / PULSES_PER_REV; // m per pulse
  float pulsesPerKm_f = 1000.0f / d_pulse;
  PULSES_PER_KM = (unsigned long)(pulsesPerKm_f + 0.5f);
  if (PULSES_PER_KM == 0) PULSES_PER_KM = 1;

  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), pulseISR, RISING);

  fare_cents = 0;
  formatDisplayFromFare();
  lastPulseMillis = millis();
}

unsigned long lastLoopMs = 0;
void loop() {
  unsigned long now = millis();
  // refresh display (multiplex) frequently
  refreshDisplay();

  // handle start/stop button (toggle)
  static bool lastStartBtn = HIGH;
  bool curStartBtn = digitalRead(BTN_STARTSTOP);
  if (lastStartBtn == HIGH && curStartBtn == LOW) {
    // pressed
    if (!running) applyStartRide();
    else applyStopRide();
    delay(200);
  }
  lastStartBtn = curStartBtn;

  // handle reset
  if (readButton(BTN_RESET)) {
    fare_cents = 0;
    pulseCount = 0;
    totalPulses = 0;
    waitingAccumMs = 0;
    running = false;
    formatDisplayFromFare();
    delay(300);
  }

  // simple settings mode: hold MODE to enter (press MODE -> toggle mode)
  static bool inSettings = false;
  static bool lastModeBtn = HIGH;
  bool curModeBtn = digitalRead(BTN_MODE);
  if (lastModeBtn == HIGH && curModeBtn == LOW) {
    inSettings = !inSettings;
    delay(200);
  }
  lastModeBtn = curModeBtn;

  if (inSettings) {
    // show alternating values on display for editing
    static int editStep = 0; // 0=startFee,1=pricePerKm,2=waitPer10min
    static unsigned long lastEditSwitch = 0;
    if (now - lastEditSwitch > 1500) {
      editStep = (editStep + 1) % 3;
      lastEditSwitch = now;
    }
    // adjust current editing item with UP/DOWN
    if (readButton(BTN_UP)) {
      if (editStep == 0) { if (startFee_cents < 9999) startFee_cents += 10; } // step 0.10
      if (editStep == 1) { if (pricePerKm_cents < 9999) pricePerKm_cents += 10; }
      if (editStep == 2) { if (waitPer10min_cents < 9999) waitPer10min_cents += 1; }
      delay(200);
    }
    if (readButton(BTN_DOWN)) {
      if (editStep == 0 && startFee_cents >= 10) startFee_cents -= 10;
      if (editStep == 1 && pricePerKm_cents >= 10) pricePerKm_cents -= 10;
      if (editStep == 2 && waitPer10min_cents >= 1) waitPer10min_cents -= 1;
      delay(200);
    }
    // display editing value
    uint16_t showVal = 0;
    if (editStep == 0) showVal = startFee_cents;
    if (editStep == 1) showVal = pricePerKm_cents;
    if (editStep == 2) showVal = waitPer10min_cents;
    // temporarily put showVal to display
    unsigned int backupFare = fare_cents;
    fare_cents = showVal;
    formatDisplayFromFare();
    // HINT: long-press MODE again to exit and save
    if (!inSettings) saveSettingsToEEPROM();
    // continue main loop
    fare_cents = backupFare;
    formatDisplayFromFare();
    continue;
  }

  // normal running logic (only when running)
  if (running) {
    // 1) distance billing: check if enough pulses for 1 km
    noInterrupts();
    unsigned long pc = pulseCount; // pulses not yet converted to km-charges
    interrupts();
    if (pc >= PULSES_PER_KM) {
      // compute how many full km units
      unsigned long fullKmUnits = pc / PULSES_PER_KM;
      // apply price per km for each full km unit
      unsigned long addCents = (unsigned long)pricePerKm_cents * fullKmUnits;
      if ((unsigned long)fare_cents + addCents > MAX_FARE_CENTS) fare_cents = MAX_FARE_CENTS;
      else fare_cents += addCents;
      // subtract handled pulses
      noInterrupts();
      pulseCount -= fullKmUnits * PULSES_PER_KM;
      interrupts();
      formatDisplayFromFare();
    }

    // 2) waiting detection
    if (now - lastPulseMillis > IDLE_MS) {
      // considered waiting
      inWaitingState = true;
    } else {
      inWaitingState = false;
      waitingAccumMs = 0; // reset waiting accumulation when moving
    }
    if (inWaitingState) {
      // accumulate waiting time
      static unsigned long lastAccumTick = 0;
      if (lastAccumTick == 0) lastAccumTick = now;
      unsigned long delta = now - lastAccumTick;
      if (delta > 0) {
        waitingAccumMs += delta;
        lastAccumTick = now;
      }
      // if reach 10 minutes, add waiting charge
      while (waitingAccumMs >= WAIT_PERIOD_MS) {
        waitingAccumMs -= WAIT_PERIOD_MS;
        if ((unsigned long)fare_cents + waitPer10min_cents > MAX_FARE_CENTS) fare_cents = MAX_FARE_CENTS;
        else fare_cents += waitPer10min_cents;
        formatDisplayFromFare();
      }
    } else {
      // not waiting
    }
  }
  // small delay to allow display refresh etc
  delay(5);
}
