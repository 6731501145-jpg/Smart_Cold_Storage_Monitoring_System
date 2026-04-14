#include "DHT.h"

// --- ตั้งค่า Pin อุปกรณ์ ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define DOOR_SENSOR_PIN 27
const int trigPin = 5;
const int echoPin = 18;

// --- ตั้งค่าเซนเซอร์ ---
DHT dht(DHTPIN, DHTTYPE);

// --- Moving Average Filter Setup ---
const int numReadings = 10;
float readingsHI[numReadings], readingsStock[numReadings], readingsDew[numReadings];
int readIndex = 0;
float totalHI = 0, totalStock = 0, totalDew = 0;

// --- ตัวแปรสำหรับจับเวลา (Timing) ---
unsigned long doorOpenStartTime = 0;
unsigned long lastOpenDuration = 0;
bool isDoorOpen = false;

// --- ค่ามาตรฐานชั้นวาง (Calibration) ---
const float emptyDist = 100.0; // ระยะพื้น (ปรับตามที่คุณวัดได้)
const float fullDist = 10.0;   // ระยะของเต็ม (ปรับตามที่คุณวัดได้)

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);

  for (int i = 0; i < numReadings; i++) {
    readingsHI[i] = 0; readingsStock[i] = 0; readingsDew[i] = 0;
  }
  Serial.println(">>> System Fully Integrated: Monitoring & Timing <<<");
}

void loop() {
  // --- 1. การจับเวลาประตู (Magnetic Sensor Logic) ---
  int doorState = digitalRead(DOOR_SENSOR_PIN);
  if (doorState == HIGH && !isDoorOpen) { // จังหวะประตูถูกเปิด
    doorOpenStartTime = millis();
    isDoorOpen = true;
  }
  if (doorState == LOW && isDoorOpen) { // จังหวะประตูเพิ่งปิด
    lastOpenDuration = (millis() - doorOpenStartTime) / 1000;
    isDoorOpen = false;
  }

  // --- 2. อ่านค่า Raw Data จากเซนเซอร์ ---
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  float distance = pulseIn(echoPin, HIGH) * 0.034 / 2;
  float rawStock = constrain(((emptyDist - distance) / (emptyDist - fullDist)) * 100, 0, 100);

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) return;

  // --- 3. คำนวณ Analytics (Heat Index & Dew Point) ---
  float rawHI = dht.computeHeatIndex(t, h, false);
  float a = 17.27, b = 237.7;
  float alpha = ((a * t) / (b + t)) + log(h / 100.0);
  float rawDewPoint = (b * alpha) / (a - alpha);

  // --- 4. กรองข้อมูล (Moving Average Filter) ---
  totalStock -= readingsStock[readIndex];
  totalHI -= readingsHI[readIndex];
  totalDew -= readingsDew[readIndex];

  readingsStock[readIndex] = rawStock;
  readingsHI[readIndex] = rawHI;
  readingsDew[readIndex] = rawDewPoint;

  totalStock += readingsStock[readIndex];
  totalHI += readingsHI[readIndex];
  totalDew += readingsDew[readIndex];

  readIndex = (readIndex + 1) % numReadings;

  float avgStock = totalStock / numReadings;
  float avgHI = totalHI / numReadings;
  float avgDewPoint = totalDew / numReadings;

  // --- 5. ส่งข้อมูลออก Serial (Format สำหรับส่งต่อ SQL) ---
  Serial.print("STOCK:");  Serial.print(avgStock, 1);
  Serial.print("|HI:");     Serial.print(avgHI, 1);
  Serial.print("|DEW:");    Serial.print(avgDewPoint, 1);
  Serial.print("|DOOR:");   Serial.print(doorState == LOW ? "CLOSED" : "OPEN");
  Serial.print("|TIME:");   Serial.print(lastOpenDuration); // วินาทีที่เปิดล่าสุด
  Serial.print("|TEMP:");   Serial.print(t, 1);
  Serial.print("|HUM:");    Serial.println(h, 1);

  delay(1000); // อัปเดตข้อมูลทุก 1 วินาที
}