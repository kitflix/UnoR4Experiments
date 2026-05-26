#include <Arduino.h>
#include <math.h>

// ============================================================
// ANOMALY DETECTION PARAMETERS
// ============================================================
constexpr uint16_t TRAINING_SAMPLES = 60; 
constexpr float SIGMA_THRESHOLD = 3.5f;     
constexpr float EMA_ALPHA = 0.15f;          // Smooths ambient noise, keeps thermal trends
constexpr uint32_t SAMPLE_INTERVAL_MS = 100; // 10Hz sampling rate for highly responsive video

// ============================================================
// GLOBAL VARIABLES
// ============================================================
float baselineMean = 0.0f;
float baselineStdDev = 0.0f;
float filteredTemp = -1.0f; 
uint32_t lastSampleTime = 0;

// ============================================================
// INTERNAL TEMPERATURE SENSOR (No Matrix Interference)
// ============================================================
float readRawInternalTemperatureC() {
  const uint8_t TEMP_CHANNEL = 0x1E;

  analogRead(TEMP_CHANNEL);
  delayMicroseconds(50); // Standard hardware settling time
  uint16_t raw = analogRead(TEMP_CHANNEL);

  if (raw == 0 || raw >= 4095) {
    return NAN;
  }

  // Convert raw 12-bit ADC value to relative Celsius indicators
  float voltage = (raw / 4095.0f) * 3.3f;
  return ((voltage - 0.76f) / 0.0025f) + 25.0f;
}

float readFilteredTemperatureC() {
  float raw = readRawInternalTemperatureC();
  if (isnan(raw)) return filteredTemp; 

  if (filteredTemp < 0.0f) {
    filteredTemp = raw; // Seed filter on first run
  }

  // Apply Exponential Moving Average filter
  filteredTemp = (EMA_ALPHA * raw) + ((1.0f - EMA_ALPHA) * filteredTemp);
  return filteredTemp;
}

// ============================================================
// TRAINING PHASE
// ============================================================
void trainBaseline() {
  Serial.println("\n=========================================");
  Serial.println("  TINYML LOCAL TRAINING PHASE STARTING   ");
  Serial.println("=========================================");
  Serial.print("Learning environmental baseline noise profile");
  
  // Prime the filter
  for(int i = 0; i < 10; i++) {
    readFilteredTemperatureC();
    delay(30);
  }

  float sum = 0.0f;
  float* samples = new float[TRAINING_SAMPLES];

  for (uint16_t i = 0; i < TRAINING_SAMPLES; i++) {
    float temp = readFilteredTemperatureC();
    samples[i] = temp;
    sum += temp;
    
    if (i % 10 == 0) Serial.print("."); // Visual loading indicators
    delay(SAMPLE_INTERVAL_MS);
  }

  baselineMean = sum / TRAINING_SAMPLES;

  // Compute Standard Deviation (Sigma)
  float variance = 0.0f;
  for (uint16_t i = 0; i < TRAINING_SAMPLES; i++) {
    float diff = samples[i] - baselineMean;
    variance += diff * diff;
  }
  variance /= TRAINING_SAMPLES;
  baselineStdDev = sqrt(variance);

  // Hard floor to avoid zero division issues in completely static conditions
  if (baselineStdDev < 0.05f) baselineStdDev = 0.05f;

  delete[] samples; // Free memory space

  Serial.println("\n\n--> EDGE MODEL TRAINED SUCCESSFULLY! <--");
  Serial.print("Trained Mean Baseline : "); Serial.print(baselineMean, 4); Serial.println(" C");
  Serial.print("Trained Std Deviation : "); Serial.print(baselineStdDev, 4); Serial.println(" C");
  Serial.print("Anomaly Trigger Margin: > "); Serial.print(SIGMA_THRESHOLD * baselineStdDev, 4); Serial.println(" units of shift");
  Serial.println("=========================================\n");
  delay(2000); // Give the user time to read the calibration metrics on screen
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for terminal monitor window to open
  delay(500);

  analogReadResolution(12); // Use high-res 12-bit conversion tracking
  trainBaseline();
  
  lastSampleTime = millis();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  uint32_t now = millis();

  if ((now - lastSampleTime) >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;

    float currentTemp = readFilteredTemperatureC();
    float deviation = fabs(currentTemp - baselineMean);
    float threshold = SIGMA_THRESHOLD * baselineStdDev;

    // Format output to look like a pro industrial terminal stream
    Serial.print("Temp: "); Serial.print(currentTemp, 2);
    Serial.print(" C | Dev: "); Serial.print(deviation, 4);
    Serial.print(" | Threshold: "); Serial.print(threshold, 4);

    // 3-Sigma Mathematical Inference Trigger
    if (deviation > threshold) {
      Serial.println("  ❌ [⚠️ ANOMALY DETECTED - LOCAL INFERENCE HIGH]");
    } else {
      Serial.println("  | State: NORMAL (Offline Edge Logic)");
    }
  }
}_