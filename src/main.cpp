#include <Arduino.h>

#define DATA0_PIN 2  // INT0
#define DATA1_PIN 3  // INT1

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define USE_DEADTIME 0
const unsigned long DEADTIME_US = 200; // solo si USE_DEADTIME=1

volatile char bitBuffer[27];   // 26 bits + '\0'
volatile byte bitIndex = 0;
volatile unsigned long lastBitTimeMs = 0;
volatile unsigned long lastEdgeUs = 0; // para deadtime opcional

const unsigned long completeGapMs    = 20;   // gap tras bit 26
const unsigned long partialTimeoutMs = 200;  // timeout frame incompleto

inline bool acceptEdge() {
#if USE_DEADTIME
  unsigned long now = micros();
  if (now - lastEdgeUs < DEADTIME_US) return false; // evita doble conteo
  lastEdgeUs = now;
#endif
  return true;
}

void IRAM_ATTR ISR_data0() {
  if (!acceptEdge()) return;
  if (bitIndex < 26) {
    bitBuffer[bitIndex++] = '0';
    lastBitTimeMs = millis();
  }
}

void IRAM_ATTR ISR_data1() {
  if (!acceptEdge()) return;
  if (bitIndex < 26) {
    bitBuffer[bitIndex++] = '1';
    lastBitTimeMs = millis();
  }
}

void blinkOK() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(60);
  digitalWrite(LED_BUILTIN, LOW);
}

void blinkError3x() {
  for (int i = 0; i < 3; ++i) {
    digitalWrite(LED_BUILTIN, HIGH); delay(70);
    digitalWrite(LED_BUILTIN, LOW);  delay(70);
  }
}

void setup() {
  Serial.begin(9600);  // RS232 al GPS mediante MAX3232

  pinMode(DATA0_PIN, INPUT_PULLUP);
  pinMode(DATA1_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(DATA0_PIN), ISR_data0, FALLING);
  attachInterrupt(digitalPinToInterrupt(DATA1_PIN), ISR_data1, FALLING);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  memset((void*)bitBuffer, 0, sizeof(bitBuffer));

  // Debug inicial (puedes comentar esta línea también)
  // Serial.println(">> Wiegand listo (envío a RS232)");
}

void loop() {
  static char localBuffer[27];

  // Reset por frame incompleto
  noInterrupts();
  byte idx = bitIndex;
  unsigned long lastMs = lastBitTimeMs;
  interrupts();

  if (idx > 0 && idx < 26 && (millis() - lastMs > partialTimeoutMs)) {
    noInterrupts();
    bitIndex = 0;
    memset((void*)bitBuffer, 0, sizeof(bitBuffer));
    interrupts();

    // Debug opcional
    // Serial.print("[WARN] Reset por frame parcial. bits="); Serial.println(idx);

    blinkError3x();
    return;
  }

  // Procesar frame completo
  noInterrupts();
  idx = bitIndex;
  lastMs = lastBitTimeMs;
  if (idx == 26 && (millis() - lastMs > completeGapMs)) {
    strncpy(localBuffer, (const char*)bitBuffer, 26);
    localBuffer[26] = '\0';
    bitIndex = 0;
    memset((void*)bitBuffer, 0, sizeof(bitBuffer));
    interrupts();

    unsigned long raw = strtoul(localBuffer, NULL, 2);
    unsigned int cardNumber = (raw >> 1) & 0xFFFF;

    // >>> ENVÍO NORMAL A RS232 <<<
    Serial.print(cardNumber);
    Serial.print("\r\n");

    // Debug opcional
    // Serial.print("[OK] bin="); Serial.print(localBuffer);
    // Serial.print("  card="); Serial.println(cardNumber);

    blinkOK();
  } else {
    interrupts();
  }
}
