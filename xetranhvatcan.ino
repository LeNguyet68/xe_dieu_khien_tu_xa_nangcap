#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// --- CẤU HÌNH WIFI & MQTT ---
const char* ssid = "test123";
const char* password = "12345678";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic = "hust/iot/rccar/control_v1";

WiFiClient espClient;
PubSubClient client(espClient);

// --- CẤU HÌNH THẺ TỪ RC522 ---
#define RST_PIN 4
#define SS_PIN 15
MFRC522 mfrc522(SS_PIN, RST_PIN);
bool isLocked = true; // Xe mặc định bị khóa khi khởi động
String AUTHORIZED_UID = "DB C2 30 54"; // Thay mã thẻ của bạn vào đây

// --- CẤU HÌNH CẢM BIẾN SIÊU ÂM HC-SR04 ---
const int TRIG_PIN = 25;
const int ECHO_PIN = 26;
unsigned long lastMeasureTime = 0;

// --- CẤU HÌNH ĐỘNG CƠ ---
int Speed = 220;
int enA = 23;
int enB = 5;
int IN1 = 19;
int IN2 = 18;
int IN3 = 22;
int IN4 = 21;
char currentAction = 'S'; // Biến lưu trạng thái di chuyển hiện tại

void forward() {
  currentAction = 'F';
  ledcWrite(enA, Speed); ledcWrite(enB, Speed);
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}

void backward() {
  currentAction = 'B';
  ledcWrite(enA, Speed); ledcWrite(enB, Speed);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
}

void left() {
  currentAction = 'L';
  ledcWrite(enA, Speed); ledcWrite(enB, Speed);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}

void right() {
  currentAction = 'R';
  ledcWrite(enA, Speed); ledcWrite(enB, Speed);
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
}

void stopCar() {
  currentAction = 'S';
  ledcWrite(enA, 0); ledcWrite(enB, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

// Hàm đo khoảng cách
float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout 30ms tránh treo code
  if (duration == 0) return 999.0;
  return duration * 0.034 / 2;
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (isLocked) {
    Serial.println("Xe đang khóa. Vui lòng quẹt thẻ!");
    return; // Chặn mọi lệnh điều khiển nếu chưa quẹt thẻ
  }

  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }

  if (messageTemp == "F") forward();
  else if (messageTemp == "B") backward();
  else if (messageTemp == "L") left();
  else if (messageTemp == "R") right();
  else if (messageTemp == "S") stopCar();
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {     
      client.subscribe(mqtt_topic);
    } else {     
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Khởi tạo các chân động cơ
  pinMode(enA, OUTPUT); pinMode(enB, OUTPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  ledcAttach(enA, 5000, 8);
  ledcAttach(enB, 5000, 8);
  stopCar();

  // Khởi tạo chân siêu âm
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Khởi tạo SPI tùy chỉnh cho RC522 để tránh trùng chân động cơ
  SPI.begin(14, 12, 13, 15); 
  mfrc522.PCD_Init();
  Serial.println("Đã khởi động. Vui lòng quẹt thẻ để mở khóa xe...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback); 
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 1. Logic Đọc Thẻ Từ (Chỉ quét khi xe đang bị khóa)
  if (isLocked && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uidString = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      uidString += String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      uidString += String(mfrc522.uid.uidByte[i], HEX);
    }
    uidString.trim();
    uidString.toUpperCase();

    Serial.print("Mã thẻ vừa quẹt: ");
    Serial.println(uidString);

    if (uidString == AUTHORIZED_UID) {
      isLocked = false;
      Serial.println("Mở khóa thành công! Có thể điều khiển xe.");
    } else {
      Serial.println("Thẻ sai!");
    }
    mfrc522.PICC_HaltA();
  }

  // 2. Logic Chống Va Chạm (Chỉ quét khi xe đã mở khóa và đang tiến lên)
  if (!isLocked && currentAction == 'F') {
    if (millis() - lastMeasureTime > 100) { // Quét mỗi 100ms
      lastMeasureTime = millis();
      float distance = getDistance();

  // THÊM 2 DÒNG NÀY ĐỂ XEM CẢM BIẾN CÓ SỐNG KHÔNG:
      Serial.print("Khoảng cách thực tế: ");
      Serial.println(distance);
      
      if (distance > 2.0 && distance <= 20.0) { // Bỏ qua nhiễu < 2cm
        stopCar();
        Serial.print("Phát hiện vật cản cách ");
        Serial.print(distance);
        Serial.println("cm. Đã dừng khẩn cấp!");
      }
    }
  }
}