#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- WiFi & MQTT Config ---
const char* ssid = "S";
const char* password = "123456789";
const char* mqtt_server = "10.134.72.79";

// *** สร้าง Topic ให้ตรงกับฝั่งส่ง ***
const char* topic_temp      = "room/101/temperature";
const char* topic_hum       = "room/101/humidity";
const char* topic_stock     = "room/101/stock";
const char* topic_heat      = "room/101/heat_index";
const char* topic_door      = "room/101/door";

// --- เกณฑ์ประเมินสต็อก ---
const int STOCK_FULL = 70;
const int STOCK_LOW = 30;

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

// --- ตัวแปรเก็บค่าจาก MQTT ---
float currentTemp = 0;
float currentHumid = 0;
float currentStock = 0;
float currentHeatIndex = 0;
String currentDoor = "CLOSED";
String roomName = "Room-101";

// --- ตัวแปรจับเวลาประตู ---
unsigned long doorTimerStart = 0;
bool doorTimerActive = false;
const unsigned long DOOR_THRESHOLD = 10000;  // 10 วินาที
bool doorTimeoutAlert = false;
bool isEmergency = false;  

// --- ตัวแปรจัดการหน้าจอและ Buzzer (Non-blocking) ---
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 1000; // อัปเดตหน้าจอทุก 1 วินาที
unsigned long lastBuzzerUpdate = 0;
bool buzzerState = false;

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

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  String currentTopic = String(topic);

  // รับข้อมูลมาเก็บใส่ตัวแปรไว้เงียบๆ
  if (currentTopic == topic_temp) {
    currentTemp = message.toFloat();
  } 
  else if (currentTopic == topic_hum) {
    currentHumid = message.toFloat();
  } 
  else if (currentTopic == topic_stock) {
    currentStock = message.toFloat();
  } 
  else if (currentTopic == topic_heat) {
    currentHeatIndex = message.toFloat();
  } 
  else if (currentTopic == topic_door) {
    currentDoor = message;
    if (currentDoor == "OPEN") {
      if (!doorTimerActive) {
        doorTimerStart = millis();
        doorTimerActive = true;
      }
    } else {
      doorTimerActive = false;
      doorTimeoutAlert = false;
    }
  }
}

void updateDisplayAndLeds() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("ROOM: "); display.print(roomName);
  
  display.setCursor(0, 11);
  display.print("T: "); display.print(currentTemp, 1); display.print("C");
  
  display.setCursor(64, 11);
  display.print("H: "); display.print(currentHumid, 1); display.print("%");
  
  display.setCursor(0, 22);
  display.print("STOCK: "); display.print(currentStock, 1); display.print("%");
  
  display.setCursor(0, 33);
  display.print("HEAT:  "); display.print(currentHeatIndex, 1); display.print("C");
  
  display.setCursor(0, 44);
  display.print("DOOR:  ");
  if (doorTimeoutAlert) display.print("!! LONG OPEN !!");
  else display.print(currentDoor);

  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);

  String safetyMsg = "SYSTEM OK";
  bool isDanger = false;

  // เอาเงื่อนไขฉุกเฉินมาเช็คตอนอัปเดตหน้าจอด้วย (อิงค่า isEmergency จาก loop)
  if (isEmergency || doorTimeoutAlert || currentStock <= 0) {
    isDanger = true;
    if (doorTimeoutAlert) safetyMsg = "CLOSE DOOR!";
    else if (currentHumid > 80) safetyMsg = "HIGH HUMID!";
    else if (currentTemp > 40) safetyMsg = "FIRE RISK!";
    else if (currentStock <= 0) safetyMsg = "OUT OF STOCK!";
  }

  display.setCursor(0, 56);
  display.print(isDanger ? "ALERT: " : "STATUS: ");
  display.print(isDanger ? safetyMsg : (currentStock < STOCK_LOW ? "LOW STOCK" : "NORMAL"));
  display.display();

  // ควบคุม LED
  if (isDanger) {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_ORANGE, LOW);
    digitalWrite(LED_GREEN, LOW);
  } else if (currentStock < STOCK_LOW) {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_ORANGE, HIGH);
    digitalWrite(LED_RED, LOW);
  } else {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_ORANGE, LOW);
    digitalWrite(LED_RED, LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "SereneReceiver-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      
      client.subscribe(topic_temp);
      client.subscribe(topic_hum);
      client.subscribe(topic_stock);
      client.subscribe(topic_heat);
      client.subscribe(topic_door);
      
    } else {
      delay(5000); // delay ตรงนี้โอเค เพราะจงใจให้รอถ้าเน็ตหลุด
    }
  }
}

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
  display.display();

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // อัปเดตสถานะฉุกเฉินตลอดเวลา ให้ระบบรู้ตัวทันที
  isEmergency = (currentTemp > 40 || currentHumid > 80); 

  // 1. เช็คว่าเปิดประตูค้างเกินกำหนดหรือยัง
  if (doorTimerActive && (millis() - doorTimerStart > DOOR_THRESHOLD)) {
    doorTimeoutAlert = true;
  }

  // 2. อัปเดตหน้าจอแบบหน่วงเวลา (ทุก 1 วินาที) ป้องกันจอค้าง
  if (millis() - lastDisplayUpdate > DISPLAY_INTERVAL) {
    updateDisplayAndLeds();
    lastDisplayUpdate = millis();
  }

  // 3. ควบคุม Buzzer แบบไม่บล็อกโค้ดส่วนอื่น
  if (isEmergency || doorTimeoutAlert || currentStock <= 0) {
    if (millis() - lastBuzzerUpdate > 200) {
      buzzerState = !buzzerState; 
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      lastBuzzerUpdate = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW); // ปิดเสียงถ้าทุกอย่างปกติ
    buzzerState = false;
  }
}
