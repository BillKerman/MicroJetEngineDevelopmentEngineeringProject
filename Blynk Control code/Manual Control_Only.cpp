// ESP32 + Blynk: MANUAL CONTROL ONLY
// V1 Slider (0..100): Controls ESC speed (1000..2000 µs)
// V2 Slider (0..100): Controls Fuel Pump (L298N)
// Physical Button (GPIO 25): Toggles Spark Plug Relay (GPIO 26)
//
// Libraries: BlynkSimpleEsp32, ESP32Servo

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <BlynkTimer.h>

char auth[] = "YOUR_BLYNK_AUTH_TOKEN";
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASS";

// === ESC (V1) Pins & Objects ===
const int escPin = 18;
Servo esc;

volatile int currentPulseWidth = 1000;
volatile int targetPulseWidth  = 1000;

// === Fuel Pump (V2) Pins & Settings ===
const int pumpEN = 23;
const int pumpIN1 = 22;
const int pumpIN2 = 21;

const int pumpFreq = 5000;
const int pumpChannel = 0;
const int pumpResolution = 8;

// 12V supply: 5V (41.7%) = 106. 12V (100%) = 255
const int pumpMinPWM = 106;
const int pumpMaxPWM = 255;

// === Spark Plug (Button) Pins & Settings ===
const int sparkButtonPin = 25; // Physical button (wired to GND)
const int sparkRelayPin = 26;  // Relay control pin

// Debounce & State variables
volatile bool sparkRelayState = false; // Start with relay OFF
bool lastButtonState = HIGH;           // For INPUT_PULLUP, HIGH is unpressed
bool buttonState = HIGH;               // Current stable button state
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;      // 50ms debounce

BlynkTimer timer;

// === ESC (V1) Functions ===
void rampStep() {
  if (currentPulseWidth == targetPulseWidth) return;
  if (currentPulseWidth < targetPulseWidth) currentPulseWidth++;
  else currentPulseWidth--;
  esc.writeMicroseconds(currentPulseWidth);
}

void keepAlive() {
  esc.writeMicroseconds(currentPulseWidth);
}

// === Spark Plug (Button) Function ===
void checkSparkButton() {
  int reading = digitalRead(sparkButtonPin);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      
      if (buttonState == LOW) {
        sparkRelayState = !sparkRelayState;
        digitalWrite(sparkRelayPin, sparkRelayState);

        // Send update to Blynk app (e.g., to an LED widget on V3)
        Blynk.virtualWrite(V3, sparkRelayState ? 255 : 0); 

        Serial.print("Spark Button Pressed. Relay state: ");
        Serial.println(sparkRelayState ? "ON" : "OFF");
      }
    }
  }
  lastButtonState = reading; 
}


// === Utility ===
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Helper to map % (0-100) to ESC pulse (1000-2000)
int percentToPulse(int percent) {
  percent = constrain(percent, 0, 100);
  if (percent == 0) return 1000;
  return (int)round(mapFloat((float)percent, 0.0f, 100.0f, 1000.0f, 2000.0f));
}

// Helper to map % (0-100) to Pump PWM
int percentToPumpPWM(int percent) {
  percent = constrain(percent, 0, 100);
  if (percent == 0) return 0; // 0% slider = 0% PWM (OFF)
  // 1-100% slider maps to 5V-12V (106-255 PWM)
  return (int)round(mapFloat((float)percent, 1.0f, 100.0f, (float)pumpMinPWM, (float)pumpMaxPWM));
}

// === Blynk Handlers ===

// Blynk slider bound to V1 (ESC)
BLYNK_WRITE(V1) {
  int v = param.asInt();
  v = constrain(v, 0, 100);

  if (v == 0) {
    targetPulseWidth = 1000;
    currentPulseWidth = 1000;
    esc.writeMicroseconds(currentPulseWidth);
    Serial.println("Blynk V1: STOP (0)");
  } else {
    targetPulseWidth = percentToPulse(v);
    Serial.print("Blynk V1: "); Serial.print(v);
    Serial.print("% -> targetPulse: "); Serial.println(targetPulseWidth);
  }
  Blynk.virtualWrite(V1, v); // Echo back
}

// Blynk slider bound to V2 (Fuel Pump)
BLYNK_WRITE(V2) {
  int v = param.asInt();
  int pumpPWM = percentToPumpPWM(v);

  ledcWrite(pumpChannel, pumpPWM);
  Blynk.virtualWrite(V2, v);
  
  Serial.print("Blynk V2: "); Serial.print(v);
  Serial.print("% -> pumpPWM: "); Serial.println(pumpPWM);
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V1);
  Blynk.syncVirtual(V2);
  Blynk.syncVirtual(V3); // Get last known relay state on connect
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // === ESC (V1) Setup ===
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  esc.attach(escPin, 1000, 2000);
  Serial.println("--- Motor Controller Ready ---");
  esc.writeMicroseconds(2000);
  delay(2000);
  esc.writeMicroseconds(1000);
  delay(2000);
  Serial.println("ESC Armed (initial pulses sent).");

  // === Fuel Pump (V2) Setup ===
  pinMode(pumpIN1, OUTPUT);
  pinMode(pumpIN2, OUTPUT);
  digitalWrite(pumpIN1, HIGH);
  digitalWrite(pumpIN2, LOW);
  ledcSetup(pumpChannel, pumpFreq, pumpResolution);
  ledcAttachPin(pumpEN, pumpChannel);
  ledcWrite(pumpChannel, 0);
  Serial.println("Fuel Pump Controller Ready.");

  // === Spark Plug (Button) Setup ===
  pinMode(sparkButtonPin, INPUT_PULLUP); // Use internal pull-up
  pinMode(sparkRelayPin, OUTPUT);        // Set relay pin as output
  digitalWrite(sparkRelayPin, sparkRelayState); // Set initial relay state (OFF)
  Serial.println("Spark Plug Relay Ready.");
  
  // Start Wi-Fi + Blynk
  Blynk.begin(auth, ssid, pass);

  // Timers:
  timer.setInterval(5L, rampStep);        // ESC ramp
  timer.setInterval(20L, keepAlive);      // ESC keep-alive
  timer.setInterval(50L, checkSparkButton); // Check button every 50ms
}

void loop() {
  Blynk.run();
  timer.run();
}
