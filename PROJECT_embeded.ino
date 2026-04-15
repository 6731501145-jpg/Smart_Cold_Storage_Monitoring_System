#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h> 

// --- ตั้งค่าส่วนตัว ---
const char* ssid = "S";
const char* password = "123456789";
const char* mqtt_server = "10.134.72.135"; // ใช้ Local IP ตามที่ Serene ตั้งไว้
const char* mqtt_topic = "sensor/storage/data"; 

// --- เกณฑ์ประเมินสต็อก ---
const int STOCK_FULL  = 70;  
const int STOCK_LOW   = 30;  

// --- ตั้งค่าอุปกรณ์ ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define LED_GREEN 12
#define LED_ORANGE 13
#define LED_RED 14
#define BUZZER_PIN 25

WiFiClient espClient;
PubSubClient client(espClient);

// ตัวแปรเก็บค่า (เปลี่ยนจาก Dew เป็น Heat Index)
float currentTemp = 0;
float currentHumid = 0;
float currentStock = 0; 
float currentHeatIndex = 0; // <-- เปลี่ยนชื่อตัวแปรให้ตรงความหมาย
String currentDoor = "CLOSED"; 

void setup() {
  Serial.begin(115200);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  digitalWrite(BUZZER_PIN, HIGH); delay(100); digitalWrite(BUZZER_PIN, LOW);
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  Serial.print("Message arrived: ");
  Serial.println(message);

  if (String(topic) == mqtt_topic) {
    StaticJsonDocument<256> doc; 
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return; 
    }

    // ดึงค่าต่างๆ รวมถึง heat_index มาเก็บ
    currentTemp = doc["temp"];
    currentHumid = doc["hum"];
    currentStock = doc["stock"];
    currentHeatIndex = doc["heat_index"]; // <-- รับค่า Heat Index มาเก็บ
    currentDoor = doc["door"].as<String>();

    updateDisplayAndLeds();
  }
}

// จัด Layout หน้าจอใหม่
void updateDisplayAndLeds() {
  display.clearDisplay();
  display.setTextSize(1);      
  display.setTextColor(WHITE); 

  // แถว 1: แบ่งซ้าย (อุณหภูมิ) - ขวา (ความชื้น)
  display.setCursor(0, 0);
  display.print("T: "); display.print(currentTemp, 1); display.print(" C");
  display.setCursor(64, 0); 
  display.print("H: "); display.print(currentHumid, 1); display.print(" %");

  // แถว 2: สต็อก
  display.setCursor(0, 11); 
  display.print("STOCK: "); display.print(currentStock, 1); display.print(" %");

  // แถว 3: Heat Index (เปลี่ยนจาก Dew)
  display.setCursor(0, 22);
  display.print("HEAT:  "); display.print(currentHeatIndex, 1); display.print(" C"); // ใช้ตัวย่อ HEAT ให้พอดีจอ

  // แถว 4: ประตู
  display.setCursor(0, 33);
  display.print("DOOR:  "); display.print(currentDoor);

  // เส้นคั่น
  display.drawLine(0, 44, 128, 44, WHITE);

  // ประมวลผลสถานะ
  String stockMsg = "";
  String safetyMsg = "SYSTEM OK";
  bool isDanger = false;
  bool isEmergency = false; 

  if (currentTemp > 40 || currentHumid > 80) { 
    isDanger = true;
    isEmergency = true;
    safetyMsg = (currentTemp > 40) ? "! FIRE RISK !" : "! HIGH HUMID !";
  }

  if (currentStock >= STOCK_FULL) {
    stockMsg = "FULL";
  } else if (currentStock >= STOCK_LOW) {
    stockMsg = "LOW";
  } else {
    stockMsg = "EMPTY";
    isDanger = true;
  }

  // แสดงสถานะด้านล่างจอ
  display.setCursor(0, 52); 
  if (isEmergency) {
    display.print("ALERT: "); display.print(safetyMsg);
  } else {
    display.print("STATUS: "); display.print(stockMsg);
  }

  display.display();

  // ควบคุมไฟ LED และเสียง
  if (isDanger) {
    digitalWrite(LED_RED, HIGH); digitalWrite(LED_ORANGE, LOW); digitalWrite(LED_GREEN, LOW);
    if (isEmergency) {
      for(int i=0; i<2; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(50); digitalWrite(BUZZER_PIN, LOW); delay(50); }
    } else {
      digitalWrite(BUZZER_PIN, HIGH); delay(300); digitalWrite(BUZZER_PIN, LOW); 
    }
  } else if (currentStock >= STOCK_LOW && currentStock < STOCK_FULL) {
    digitalWrite(LED_GREEN, LOW); digitalWrite(LED_ORANGE, HIGH); digitalWrite(LED_RED, LOW);
  } else {
    digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_ORANGE, LOW); digitalWrite(LED_RED, LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "SereneReceiver-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
}
