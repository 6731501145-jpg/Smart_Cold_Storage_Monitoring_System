#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- ตั้งค่าส่วนตัว ---
const char* ssid = "S";
const char* password = "123456789";
const char* mqtt_server = "broker.emqx.io";  // ใช้ Broker สาธารณะลองก่อน

// --- แก้เกณฑ์ใหม่สำหรับกล่อง 30 cm ---
const int DIST_FULL  = 8;   // ระยะ 0-8 cm คือของเต็ม (ไฟเขียว)
const int DIST_LOW   = 20;  // ระยะ 9-20 cm คือของเริ่มน้อย (ไฟเหลือง)
const int DIST_EMPTY = 28;  // ระยะ > 25 cm หรือเกือบถึงพื้นคือของหมด (ไฟแดง)

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

float currentTemp = 0;
float currentDist = 0;
float currentHumid = 0;

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
  display.setTextColor(WHITE);

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

// ฟังก์ชันรับข้อมูลจาก MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println(message);  // ดูว่าค่า "45" มาถึงบอร์ดจริงๆ ไหม

  if (String(topic) == "warehouse/temp") currentTemp = message.toFloat();
  if (String(topic) == "warehouse/distance") currentDist = message.toFloat();
  if (String(topic) == "warehouse/humidity") currentHumid = message.toFloat();

  updateDisplayAndLeds();
}

void updateDisplayAndLeds() {
  display.clearDisplay();

//   

// --- ข้อมูลเซ็นเซอร์ (Data Section) ---
  display.setCursor(0, 18);
  display.print("T: "); display.print(currentTemp, 1); display.print(" C  ");
  display.print("H: "); display.print(currentHumid, 1); display.println(" %"); // เพิ่มบรรทัดนี้
  display.print("DIST: "); display.print(currentDist, 1); display.println(" cm");
  display.drawLine(0, 36, 128, 36, WHITE);

  String stockMsg = "";
  String safetyMsg = "SYSTEM OK";
  bool isDanger = false;
  bool isEmergency = false; // สำหรับไฟไหม้หรือความชื้นวิกฤต

  // --- เช็คความปลอดภัย (Temp & Humidity) ---
  if (currentTemp > 40 || currentHumid > 80) { // ถ้าร้อนไป หรือ ชื้นไป
    isDanger = true;
    isEmergency = true;
    if (currentTemp > 40) safetyMsg = "!!! FIRE RISK !!!";
    else safetyMsg = "!!! HIGH HUMID !!!";
  }

  // --- เช็คระดับสินค้า (Logic เดิม) ---
  if (currentDist > 0 && currentDist <= DIST_FULL) {
    stockMsg = "STOCK: FULL";
  } else if (currentDist > DIST_FULL && currentDist <= DIST_LOW) {
    stockMsg = "STOCK: LOW";
  } else {
    stockMsg = "STOCK: EMPTY";
    isDanger = true;
  }

  // --- สรุปสถานะไฟและเสียง ---
  if (isDanger) {
    digitalWrite(LED_RED, HIGH); digitalWrite(LED_ORANGE, LOW); digitalWrite(LED_GREEN, LOW);
    if (isEmergency) {
      // เสียงเตือนสั้นถี่สำหรับเหตุฉุกเฉิน
      for(int i=0; i<2; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(50); digitalWrite(BUZZER_PIN, LOW); delay(50); }
    } else {
      digitalWrite(BUZZER_PIN, HIGH); delay(300); digitalWrite(BUZZER_PIN, LOW); // เสียงเตือนของหมด
    }
  } else if (currentDist > DIST_FULL && currentDist <= DIST_LOW) {
    digitalWrite(LED_GREEN, LOW); digitalWrite(LED_ORANGE, HIGH); digitalWrite(LED_RED, LOW);
  } else {
    digitalWrite(LED_GREEN, HIGH); digitalWrite(LED_ORANGE, LOW); digitalWrite(LED_RED, LOW);
  }

  // --- แสดงสถานะล่างจอ ---
  display.setCursor(0, 42); display.print(stockMsg);
  display.setTextSize(2);
  display.setCursor(0, 50); display.print(isEmergency ? "ALERT!" : "NORMAL");

  display.display();
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // เปลี่ยน ID ให้ไม่ซ้ำกับใครในโลก
    String clientId = "SereneClient-" + String(random(0, 9999));

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("warehouse/temp");
      client.subscribe("warehouse/distance");
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