#include <Arduino.h>

#define DATA0_PIN 2  // INT0
#define DATA1_PIN 3  // INT1

volatile char bitBuffer[27];   // 26 bits + null terminator
volatile byte bitIndex = 0;
volatile unsigned long lastBitTime = 0;
const unsigned long timeoutMs = 100;

void ISR_data0() {
  if (bitIndex < 26) {
    bitBuffer[bitIndex++] = '0';
    lastBitTime = millis();
  }
}

void ISR_data1() {
  if (bitIndex < 26) {
    bitBuffer[bitIndex++] = '1';
    lastBitTime = millis();
  }
}

void setup() {
  Serial.begin(9600);     // RS232 al GPS

  pinMode(DATA0_PIN, INPUT_PULLUP);
  pinMode(DATA1_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(DATA0_PIN), ISR_data0, FALLING);
  attachInterrupt(digitalPinToInterrupt(DATA1_PIN), ISR_data1, FALLING);

  pinMode(LED_BUILTIN, OUTPUT);  // LED interno

  memset((void*)bitBuffer, 0, sizeof(bitBuffer));
}

void loop() {
  static char localBuffer[27];

  noInterrupts();
  if (bitIndex == 26 && (millis() - lastBitTime > timeoutMs)) {
    strncpy(localBuffer, (const char*)bitBuffer, 26);
    localBuffer[26] = '\0';
    bitIndex = 0;
    interrupts();

    unsigned long raw = strtoul(localBuffer, NULL, 2);
    unsigned int cardNumber = (raw >> 1) & 0xFFFF;

    // Enviar por RS232 al GPS
    Serial.print(cardNumber);
    Serial.print("\r\n");

    // 🔴 Prende LED cuando hay un tag detectado
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);  // breve pulso
    digitalWrite(LED_BUILTIN, LOW);

  } else {
    interrupts();
  }
}
