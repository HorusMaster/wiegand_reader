#include <Arduino.h>
#include <stdlib.h>

#define DATA0_PIN 2  // INT0
#define DATA1_PIN 3  // INT1

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// ---------- Timings ----------
const unsigned long COMPLETE_GAP_MS     = 20;    // gap tras bit 26
const unsigned long PARTIAL_TIMEOUT_MS  = 200;   // timeout frame incompleto
const unsigned long SEND_INTERVAL_MS    = 10000; // ENVIAR 1 tag cada 10 s
const unsigned long DUP_WINDOW_MS       = 300;   // evita duplicado pegado

// ---------- Debounce/Deadtime en flancos ----------
#define USE_DEADTIME 1
const unsigned long DEADTIME_US = 200;

volatile unsigned long lastEdgeUs = 0;
inline bool acceptEdge() {
#if USE_DEADTIME
  unsigned long now = micros();
  if (now - lastEdgeUs < DEADTIME_US) return false;
  lastEdgeUs = now;
#endif
  return true;
}

// ---------- Wiegand bit buffer ----------
volatile char bitBuffer[27];   // 26 bits + '\0'
volatile byte bitIndex = 0;
volatile unsigned long lastBitTimeMs = 0;

// ---------- Cola circular de tags ----------
const uint16_t QUEUE_SIZE = 256;
const bool DROP_OLDEST_ON_FULL = true;

volatile uint16_t qData[QUEUE_SIZE];
volatile uint16_t qCount = 0;
volatile uint16_t qHead  = 0;
volatile uint16_t qTail  = 0;

// ---------- Ritmo de envío ----------
unsigned long lastSendMs = 0;

// Anti-duplicado corto
uint16_t lastCardSeen = 0;
unsigned long lastCardSeenMs = 0;

// ---------- Utils cola ----------
inline void queueClear() {
  noInterrupts();
  qHead = qTail = 0;
  qCount = 0;
  interrupts();
}

inline bool queuePush(uint16_t v) {
  noInterrupts();
  if (qCount >= QUEUE_SIZE) {
    if (DROP_OLDEST_ON_FULL) {
      qTail = (qTail + 1) % QUEUE_SIZE;
      qCount--;
    } else {
      interrupts();
      return false;
    }
  }
  qData[qHead] = v;
  qHead = (qHead + 1) % QUEUE_SIZE;
  qCount++;
  interrupts();
  return true;
}

inline bool queuePop(uint16_t &v) {
  noInterrupts();
  if (qCount == 0) { interrupts(); return false; }
  v = qData[qTail];
  qTail = (qTail + 1) % QUEUE_SIZE;
  qCount--;
  interrupts();
  return true;
}

// ---------- Blink helpers ----------
void blinkReadOnce() {             // Lectura OK -> en cola
  digitalWrite(LED_BUILTIN, HIGH); delay(70);
  digitalWrite(LED_BUILTIN, LOW);
}

void blinkSendDouble() {           // Envío a GPS -> doble parpadeo
  for (int i = 0; i < 2; ++i) {
    digitalWrite(LED_BUILTIN, HIGH); delay(60);
    digitalWrite(LED_BUILTIN, LOW);  delay(60);
  }
}

void blinkError3x() {
  for (int i = 0; i < 3; ++i) {
    digitalWrite(LED_BUILTIN, HIGH); delay(70);
    digitalWrite(LED_BUILTIN, LOW);  delay(70);
  }
}

// ---------- ISRs ----------
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

void setup() {
  Serial.begin(9600);  // RS232 al GPS mediante MAX3232

  pinMode(DATA0_PIN, INPUT_PULLUP);
  pinMode(DATA1_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(DATA0_PIN), ISR_data0, FALLING);
  attachInterrupt(digitalPinToInterrupt(DATA1_PIN), ISR_data1, FALLING);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  memset((void*)bitBuffer, 0, sizeof(bitBuffer));
  queueClear();

  lastSendMs = millis();
}

void loop() {
  static char localBuffer[27];

  // --- Snapshot seguro de estado compartido ---
  byte idx;
  unsigned long lastMs;
  noInterrupts();
  idx    = bitIndex;
  lastMs = lastBitTimeMs;
  interrupts();

  // ----- Reset por frame incompleto -----
  if (idx > 0 && idx < 26 && (millis() - lastMs > PARTIAL_TIMEOUT_MS)) {
    noInterrupts();
    bitIndex = 0;
    memset((void*)bitBuffer, 0, sizeof(bitBuffer));
    interrupts();
    blinkError3x();
  }

  // --- Tomar otro snapshot para “frame completo” ---
  noInterrupts();
  idx    = bitIndex;
  lastMs = lastBitTimeMs;
  interrupts();

  // ----- Procesar frame completo -----
  if (idx == 26 && (millis() - lastMs > COMPLETE_GAP_MS)) {
    noInterrupts();
    for (byte i = 0; i < 26; ++i) localBuffer[i] = bitBuffer[i];
    localBuffer[26] = '\0';
    bitIndex = 0;
    memset((void*)bitBuffer, 0, sizeof(bitBuffer));
    interrupts();

    unsigned long raw = strtoul(localBuffer, NULL, 2);
    uint16_t cardNumber = (raw >> 1) & 0xFFFF;

    unsigned long now = millis();
    if (!(cardNumber == lastCardSeen && (now - lastCardSeenMs) < DUP_WINDOW_MS)) {
      if (queuePush(cardNumber)) {
        lastCardSeen = cardNumber;
        lastCardSeenMs = now;
        blinkReadOnce();   // <-- parpadeo único al encolar
      } else {
        blinkError3x();    // cola llena
      }
    }
  }

  // ----- Enviar 1 tag cada SEND_INTERVAL_MS -----
  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    uint16_t card;
    if (queuePop(card)) {
      Serial.print(card);
      Serial.print("\r\n");
      blinkSendDouble();   // <-- doble parpadeo al enviar
    }
    lastSendMs = millis();
  }
}
