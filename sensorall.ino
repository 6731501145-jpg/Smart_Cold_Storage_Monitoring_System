#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>

// --- WiFi & MQTT Config ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "broker.hivemq.com"; // หรือ IP ของเครื่องคุณ
const char* mqtt_topic = "sensor/storage/data";

WiFiClient espClient;
PubSubClient client(espClient);

// --- Pin Setup ---
#define DHTPIN 4
#define DHTTYPE DHT22
#define DOOR_SENSOR_PIN 27
const int trigPin = 5;
const int echoPin = 18;

DHT dht(DHTPIN, DHTTYPE);

// --- Filter & Variables ---
const int numReadings = 10;
float readingsHI[numReadings], readingsStock[numReadings], readingsDew[numReadings];
int readIndex = 0;
float totalHI = 0, totalStock = 0, totalDew = 0;

unsigned long doorOpenStartTime = 0;
unsigned long lastOpenDuration = 0;
bool isDoorOpen = false;

const float emptyDist = 100.0;
const float fullDist = 10.0;

// ฟังก์ชันเชื่อมต่อ WiFi
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// ฟังก์ชันเชื่อมต่อ MQTT (Reconnect อัตโนมัติ)
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
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
    readingsHI[i] = 0; readingsStock[i] = 0; readingsDew[i] = 0;
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // --- 1. Door Logic ---
  int doorState = digitalRead(DOOR_SENSOR_PIN);
  if (doorState == HIGH && !isDoorOpen) { 
    doorOpenStartTime = millis();
    isDoorOpen = true;
  }
  if (doorState == LOW && isDoorOpen) { 
    lastOpenDuration = (millis() - doorOpenStartTime) / 1000;
    isDoorOpen = false;
  }

  // --- 2. Sensor Readings ---
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  float distance = pulseIn(echoPin, HIGH) * 0.034 / 2;
  float rawStock = constrain(((emptyDist - distance) / (emptyDist - fullDist)) * 100, 0, 100);

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) return;

  // --- 3. Analytics ---
  float rawHI = dht.computeHeatIndex(t, h, false);
  float a = 17.27, b = 237.7;
  float alpha = ((a * t) / (b + t)) + log(h / 100.0);
  float rawDewPoint = (b * alpha) / (a - alpha);

  // --- 4. Moving Average Filter ---
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

  // --- 5. Publish to MQTT (JSON Format) ---
  // สร้าง String ในรูปแบบ JSON เพื่อให้ฝั่ง Server/SQL นำไป parse ง่ายๆ
  String payload = "{";
  payload += "\"stock\":" + String(avgStock, 1) + ",";
  payload += "\"heat_index\":" + String(avgHI, 1) + ",";
  payload += "\"dew_point\":" + String(avgDewPoint, 1) + ",";
  payload += "\"door\":\"" + String(doorState == LOW ? "CLOSED" : "OPEN") + "\",";
  payload += "\"last_open_sec\":" + String(lastOpenDuration) + ",";
  payload += "\"temp\":" + String(t, 1) + ",";
  payload += "\"hum\":" + String(h, 1);
  payload += "}";

  Serial.println("Publishing: " + payload);
  client.publish(mqtt_topic, payload.c_str());

  delay(2000); // MQTT ไม่ควรส่งถี่เกินไป (แนะนำ 2 วินาทีขึ้นไป)
}
