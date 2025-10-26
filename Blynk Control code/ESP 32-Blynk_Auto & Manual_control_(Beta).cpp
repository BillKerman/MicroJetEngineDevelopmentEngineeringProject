// ESP32 + Blynk Engine Controller - CLOUD CONTROLLED ONLY
// Manual Mode: V1 (ESC), V2 (Pump), V3 (Spark Toggle)
// Auto Mode: V10 (Set ESC), V11 (Set Spark Period), V12 (Set Pump)
// Mode Control: V13 (Switch Manual/Auto)
//
// Libraries: BlynkSimpleEsp32, ESP32Servo

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>

char auth[] = "YOUR_BLYNK_AUTH_TOKEN";
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASS";

// === Global Mode ===
volatile bool isManualMode = true;

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
const int pumpMinPWM = 106;  // Corresponds to ~5V
const int pumpMaxPWM = 255;  // Corresponds to ~12V

// === Spark Plug (V3) Pin ===
const int sparkRelayPin = 26;
volatile bool sparkRelayState = false;

// === Automatic Sequence Globals ===
volatile int autoSequenceState = 0;
unsigned long autoStateTimer = 0;
unsigned long lastSparkToggle = 0;
bool autoSparkRunning = false;

// === Auto-Mode Configurable Parameters ===
volatile int targetAutoESC = 80;              // Default 80%
volatile int targetAutoSparkPeriodMs = 2000;  // Default 2000ms (1s ON, 1s OFF)
volatile int targetAutoPump = 50;             // Default 50%

// === ESC Ramping Function ===
void rampStep() {
  if (currentPulseWidth == targetPulseWidth) return;
  if (currentPulseWidth < targetPulseWidth) currentPulseWidth++;
  else currentPulseWidth--;
  esc.writeMicroseconds(currentPulseWidth);
}

// === Utility Functions ===
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int percentToPulse(int percent) {
  percent = constrain(percent, 0, 100);
  if (percent == 0) return 1000;
  return (int)round(mapFloat((float)percent, 0.0f, 100.0f, 1000.0f, 2000.0f));
}

int percentToPumpPWM(int percent) {
  percent = constrain(percent, 0, 100);
  if (percent == 0) return 0;
  return (int)round(mapFloat((float)percent, 1.0f, 100.0f, (float)pumpMinPWM, (float)pumpMaxPWM));
}

// === Blynk Manual Control Handlers ===
BLYNK_WRITE(V1) { // Manual ESC Control
  if (!isManualMode) {
    Blynk.virtualWrite(V1, 0);
    return;
  }
  
  int v = param.asInt();
  v = constrain(v, 0, 100);
  
  if (v == 0) {
    targetPulseWidth = 1000;
    currentPulseWidth = 1000;
    esc.writeMicroseconds(currentPulseWidth);
    Serial.println("Blynk V1: STOP (0%)");
  } else {
    targetPulseWidth = percentToPulse(v);
    Serial.print("Blynk V1: "); Serial.print(v);
    Serial.print("% -> targetPulse: "); Serial.println(targetPulseWidth);
  }
  Blynk.virtualWrite(V1, v);
}

BLYNK_WRITE(V2) { // Manual Pump Control
  if (!isManualMode) {
    Blynk.virtualWrite(V2, 0);
    return;
  }
  
  int v = param.asInt();
  v = constrain(v, 0, 100);
  int pumpPWM = percentToPumpPWM(v);
  ledcWrite(pumpChannel, pumpPWM);
  Blynk.virtualWrite(V2, v);
  Serial.print("Blynk V2: "); Serial.print(v);
  Serial.print("% -> pumpPWM: "); Serial.println(pumpPWM);
}

BLYNK_WRITE(V3) { // Manual Spark Control (Button Widget)
  if (!isManualMode) {
    Blynk.virtualWrite(V3, 0);
    return;
  }
  
  int v = param.asInt();
  sparkRelayState = (v == 1 || v == 255);
  digitalWrite(sparkRelayPin, sparkRelayState);
  Blynk.virtualWrite(V3, sparkRelayState ? 255 : 0);
  Serial.print("Blynk V3: Spark ");
  Serial.println(sparkRelayState ? "ON" : "OFF");
}

// === Blynk Auto-Mode Configuration Handlers ===
BLYNK_WRITE(V10) { // Set Auto ESC Target
  if (!isManualMode) return;
  targetAutoESC = param.asInt();
  targetAutoESC = constrain(targetAutoESC, 0, 100);
  Blynk.virtualWrite(V10, targetAutoESC);
  Serial.print("Set Auto-Mode ESC Target: "); Serial.println(targetAutoESC);
}

BLYNK_WRITE(V11) { // Set Auto Spark Period
  if (!isManualMode) return;
  targetAutoSparkPeriodMs = param.asInt();
  targetAutoSparkPeriodMs = constrain(targetAutoSparkPeriodMs, 200, 60000);
  Blynk.virtualWrite(V11, targetAutoSparkPeriodMs);
  Serial.print("Set Auto-Mode Spark Period (ms): "); Serial.println(targetAutoSparkPeriodMs);
}

BLYNK_WRITE(V12) { // Set Auto Pump Target
  if (!isManualMode) return;
  targetAutoPump = param.asInt();
  targetAutoPump = constrain(targetAutoPump, 0, 100);
  Blynk.virtualWrite(V12, targetAutoPump);
  Serial.print("Set Auto-Mode Pump Target: "); Serial.println(targetAutoPump);
}

BLYNK_WRITE(V13) { // Mode Switch (Manual/Auto)
  int v = param.asInt();
  if (v == 1 || v == 255) {
    // Switch to AUTO mode
    if (isManualMode) {
      startAutomaticSequence();
    }
  } else {
    // Switch to MANUAL mode
    if (!isManualMode) {
      enterManualMode();
    }
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V1, V2, V3, V10, V11, V12, V13);
  
  if (isManualMode) {
    Blynk.virtualWrite(V4, 255);  // Manual mode LED ON
    Blynk.virtualWrite(V5, 0);    // Auto mode LED OFF
    Blynk.virtualWrite(V13, 0);   // Mode button OFF
  } else {
    Blynk.virtualWrite(V4, 0);
    Blynk.virtualWrite(V5, 255);
    Blynk.virtualWrite(V13, 255); // Mode button ON
  }
}

// === Auto-Mode Spark Toggle Function ===
void autoToggleSpark() {
  if (!autoSparkRunning) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastSparkToggle >= (targetAutoSparkPeriodMs / 2)) {
    sparkRelayState = !sparkRelayState;
    digitalWrite(sparkRelayPin, sparkRelayState);
    Blynk.virtualWrite(V3, sparkRelayState ? 255 : 0);
    lastSparkToggle = currentTime;
    Serial.print("Auto-Spark Toggle: ");
    Serial.println(sparkRelayState ? "ON" : "OFF");
  }
}

// === Enter Manual Mode ===
void enterManualMode() {
  Serial.println("=== ENTERING MANUAL MODE ===");
  isManualMode = true;
  autoSequenceState = 0;
  autoSparkRunning = false;

  // SAFETY: Reset all systems to OFF
  targetPulseWidth = 1000;
  currentPulseWidth = 1000;
  esc.writeMicroseconds(1000);
  ledcWrite(pumpChannel, 0);
  sparkRelayState = false;
  digitalWrite(sparkRelayPin, sparkRelayState);

  // Update Blynk UI
  Blynk.virtualWrite(V1, 0);
  Blynk.virtualWrite(V2, 0);
  Blynk.virtualWrite(V3, 0);
  Blynk.virtualWrite(V4, 255);  // Manual LED ON
  Blynk.virtualWrite(V5, 0);    // Auto LED OFF
  Blynk.virtualWrite(V13, 0);   // Mode button OFF
  
  Serial.println("All systems reset to safe state.");
}

// === Start Automatic Sequence ===
void startAutomaticSequence() {
  Serial.println("=== ENTERING AUTOMATIC MODE ===");
  
  // Reset to safe state first
  targetPulseWidth = 1000;
  currentPulseWidth = 1000;
  esc.writeMicroseconds(1000);
  ledcWrite(pumpChannel, 0);
  sparkRelayState = false;
  digitalWrite(sparkRelayPin, sparkRelayState);
  autoSparkRunning = false;
  
  // Enable auto-mode
  isManualMode = false;
  autoSequenceState = 1;
  
  Serial.print("Using Parameters - ESC: "); Serial.print(targetAutoESC);
  Serial.print("%, Spark Period: "); Serial.print(targetAutoSparkPeriodMs);
  Serial.print("ms, Pump: "); Serial.print(targetAutoPump); Serial.println("%");
  
  // Update Blynk UI
  Blynk.virtualWrite(V1, 0);
  Blynk.virtualWrite(V2, 0);
  Blynk.virtualWrite(V3, 0);
  Blynk.virtualWrite(V4, 0);    // Manual LED OFF
  Blynk.virtualWrite(V5, 255);  // Auto LED ON
  Blynk.virtualWrite(V13, 255); // Mode button ON
}

// === Auto-Mode State Machine ===
void runAutomaticSequence() {
  if (isManualMode || autoSequenceState == 0) {
    return;
  }

  switch (autoSequenceState) {
    case 1: // Set ESC to 10%
      Serial.println("Auto: State 1 - ESC 10%");
      targetPulseWidth = percentToPulse(10);
      autoStateTimer = millis();
      autoSequenceState = 2;
      break;
    
    case 2: // Wait 2s, then go to 20%
      if (millis() - autoStateTimer > 2000) {
        Serial.println("Auto: State 2 - ESC 20%");
        targetPulseWidth = percentToPulse(20);
        autoStateTimer = millis();
        autoSequenceState = 3;
      }
      break;

    case 3: // Wait 2s, then go to 30%
      if (millis() - autoStateTimer > 2000) {
        Serial.println("Auto: State 3 - ESC 30%");
        targetPulseWidth = percentToPulse(30);
        autoStateTimer = millis();
        autoSequenceState = 4;
      }
      break;

    case 4: // Wait 2s, then go to 50%
      if (millis() - autoStateTimer > 2000) {
        Serial.println("Auto: State 4 - ESC 50%");
        targetPulseWidth = percentToPulse(50);
        autoStateTimer = millis();
        autoSequenceState = 5;
      }
      break;

    case 5: // Wait 10s at 50% (Check noise)
      if (millis() - autoStateTimer > 10000) {
        Serial.println("Auto: State 5 - Noise check complete at 50%");
        autoSequenceState = 6;
      }
      break;

    case 6: // Set ESC to Target %
      Serial.print("Auto: State 6 - ESC "); Serial.print(targetAutoESC); Serial.println("%");
      targetPulseWidth = percentToPulse(targetAutoESC);
      autoStateTimer = millis();
      autoSequenceState = 7;
      break;
    
    case 7: // Wait 10s at Target %
      if (millis() - autoStateTimer > 10000) {
        Serial.print("Auto: State 7 - Stabilization complete at "); 
        Serial.print(targetAutoESC); Serial.println("%");
        autoSequenceState = 8;
      }
      break;

    case 8: // Start Spark Plug
      Serial.print("Auto: State 8 - Starting spark (Period: ");
      Serial.print(targetAutoSparkPeriodMs); Serial.println("ms)");
      autoSparkRunning = true;
      lastSparkToggle = millis();
      autoStateTimer = millis();
      autoSequenceState = 9;
      break;
    
    case 9: // Wait 2s, then start fuel pump
      if (millis() - autoStateTimer > 2000) {
        Serial.print("Auto: State 9 - Starting fuel pump at ");
        Serial.print(targetAutoPump); Serial.println("%");
        ledcWrite(pumpChannel, percentToPumpPWM(targetAutoPump));
        autoSequenceState = 10;
      }
      break;

    case 10: // Auto-mode running - steady state
      // System is fully running
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 Engine Controller - CLOUD CONTROLLED ===");

  // === ESC Setup ===
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  esc.attach(escPin, 1000, 2000);
  Serial.println("Arming ESC...");
  esc.writeMicroseconds(2000);
  delay(2000);
  esc.writeMicroseconds(1000);
  delay(2000);
  Serial.println("ESC Armed and Ready.");

  // === Fuel Pump Setup (L298N) ===
  pinMode(pumpIN1, OUTPUT);
  pinMode(pumpIN2, OUTPUT);
  digitalWrite(pumpIN1, HIGH);  // Forward direction
  digitalWrite(pumpIN2, LOW);
  ledcSetup(pumpChannel, pumpFreq, pumpResolution);
  ledcAttachPin(pumpEN, pumpChannel);
  ledcWrite(pumpChannel, 0);  // Start OFF
  Serial.println("Fuel Pump Controller Ready.");

  // === Spark Plug Relay Setup ===
  pinMode(sparkRelayPin, OUTPUT);
  digitalWrite(sparkRelayPin, LOW);  // Start OFF
  Serial.println("Spark Plug Relay Ready.");

  // === Connect to Blynk ===
  Serial.println("Connecting to WiFi and Blynk...");
  Blynk.begin(auth, ssid, pass);
  Serial.println("Connected!");

  Serial.println("=== System Ready - MANUAL MODE ===");
}

void loop() {
  Blynk.run();
  
  // Run continuous tasks
  rampStep();
  runAutomaticSequence();
  
  // Auto spark toggling
  if (!isManualMode && autoSparkRunning) {
    autoToggleSpark();
  }
  
  delay(5);  // Small delay for stability
}