#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- WiFi & MQTT Config ---
const char* ssid = "S";
const char* password = "123456789";
const char* mqtt_server = "10.134.72.79";

// *** ตั้งค่าเลขห้อง และกำหนด Topic แยกย่อย ***
const char* room_number = "Room-101";

// สร้าง Topic แยกแต่ละตัว
const char* topic_temp = "room/101/temperature";
const char* topic_hum = "room/101/humidity";
const char* topic_stock = "room/101/stock";
const char* topic_heat = "room/101/heat_index";
const char* topic_dew = "room/101/dew_point";
const char* topic_door = "room/101/door";
const char* topic_door_sec = "room/101/last_open_sec";

#define MQTT_DHT_TOPIC "dht/data"

WiFiClient espClient;
PubSubClient client(espClient);

// --- Pin Setup ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define DOOR_SENSOR_PIN 17
const int trigPin = 5;
const int echoPin = 18;

DHT dht(DHTPIN, DHTTYPE);

// --- Filter & Variables ---
const int numReadings = 10;
float readingsHI[numReadings], readingsStock[numReadings], readingsDew[numReadings];
int readIndex = 0;
float totalHI = 0, totalStock = 0, totalDew = 0;

// --- ประตู (Door Variables) ---
unsigned long doorOpenStartTime = 0;
unsigned long currentOpenDuration = 0;
bool isDoorOpen = false;

const float emptyDist = 100.0;
const float fullDist = 10.0;
float lastGoodDistance = emptyDist;

void setup_wifi() {
  delay(10);
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32-Pub-" + String(room_number) + "-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);

  dht.begin();
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);

  for (int i = 0; i < numReadings; i++) {
    readingsHI[i] = 0;
    readingsStock[i] = 100.0;
    readingsDew[i] = 0;
  }
  totalStock = 100.0 * numReadings;
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // --- 1. Door Logic ---
  int doorState = digitalRead(DOOR_SENSOR_PIN);
  if (doorState == HIGH) {
    if (!isDoorOpen) {
      doorOpenStartTime = millis();
      isDoorOpen = true;
    }
    currentOpenDuration = (millis() - doorOpenStartTime) / 1000;
  } else {
    isDoorOpen = false;
    currentOpenDuration = 0;
  }

  // --- 2. Sensor Readings ---
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // [แก้สเตป 1] แยกรับค่าเวลาออกมาก่อน 
  long duration = pulseIn(echoPin, HIGH, 30000);
  float distance = 0.0;

  if (duration == 0) {
    // ถ้าคลื่นสะท้อนกลับมาไม่ทัน (เกินระยะ) ให้ใช้ค่าเดิมไปก่อน
    distance = lastGoodDistance;
  } else {
    // แปลงเวลาเป็นระยะทาง
    distance = duration * 0.034 / 2.0;
  }

  if (distance <= 0.0 || distance > 400.0) {
    distance = lastGoodDistance;
  } else {
    lastGoodDistance = distance;
  }

  // [แก้สเตป 2] ใส่ .0 ให้เลข 0 และ 100 เพื่อกันการปัดเศษทศนิยมเพี้ยน
  float rawStock = constrain(((emptyDist - distance) / (emptyDist - fullDist)) * 100.0, 0.0, 100.0);
  
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // --- 3. Analytics ---
  float rawHI = dht.computeHeatIndex(t, h, false);
  float a = 17.27, b = 237.7;
  float alpha = ((a * t) / (b + t)) + log(h / 100.0);
  float rawDewPoint = (b * alpha) / (a - alpha);

  totalStock = totalStock - readingsStock[readIndex] + rawStock;
  totalHI = totalHI - readingsHI[readIndex] + rawHI;
  totalDew = totalDew - readingsDew[readIndex] + rawDewPoint;

  readingsStock[readIndex] = rawStock;
  readingsHI[readIndex] = rawHI;
  readingsDew[readIndex] = rawDewPoint;
  readIndex = (readIndex + 1) % numReadings;

  // คำนวณค่าเฉลี่ย
  float avgStock = totalStock / numReadings;
  float avgHI = totalHI / numReadings;
  float avgDew = totalDew / numReadings;

  StaticJsonDocument<200> doc;
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["stock"] = avgStock;
  doc["heat"] = avgHI;
  doc["dew"] = avgDew;
  doc["door"] = (doorState == HIGH) ? "OPEN" : "CLOSED";
  doc["door_sec"] = currentOpenDuration;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(MQTT_DHT_TOPIC, jsonBuffer);
  Serial.println(jsonBuffer);


  // --- 4. Publish แยก Topic ---
  Serial.println("Publishing data to separate topics...");

  client.publish(topic_temp, String(t, 1).c_str());
  client.publish(topic_hum, String(h, 1).c_str());
  client.publish(topic_stock, String(avgStock, 1).c_str());
  client.publish(topic_heat, String(avgHI, 1).c_str());
  client.publish(topic_dew, String(avgDew, 1).c_str());
  client.publish(topic_door, (doorState == HIGH) ? "OPEN" : "CLOSED");
  client.publish(topic_door_sec, String(currentOpenDuration).c_str());

  Serial.println("Publish complete!\n");
  delay(2000);
}
