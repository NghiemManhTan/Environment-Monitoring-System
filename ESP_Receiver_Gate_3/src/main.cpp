#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

const char* ssid = "VGU_Student_Guest";
const char* password = "";

String renderApiUrl = "https://iot-env-monitor.onrender.com/data/sensor";

#define LORA_RX 16
#define LORA_TX 17
#define PIN_M0 4
#define PIN_M1 5

typedef struct struct_message {
  float temperature;
  float humidity;
  float lux;
  int rainDO;
  int rainAO;
  int soilDO;
  int soilAO;
  float uvVoltage;
} struct_message;

struct_message incomingData;
struct_message finalData;

bool hasNewData = false; 

unsigned long previousMillis = 0;
const long postInterval = 5000; 

// Biến cho tính năng Auto-Reconnect
unsigned long previousWifiMillis = 0;
const long wifiCheckInterval = 10000; 

void postToRender() {
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
  HTTPClient http;
  http.setTimeout(30000);
  
  http.begin(*client, renderApiUrl);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"temperature\":" + String(finalData.temperature) + ",";
  json += "\"humidity\":"    + String(finalData.humidity) + ",";
  json += "\"lux\":"         + String(finalData.lux) + ",";
  json += "\"rainDO\":"      + String(finalData.rainDO) + ",";
  json += "\"rainAO\":"      + String(finalData.rainAO) + ",";
  json += "\"soilDO\":"      + String(finalData.soilDO) + ",";
  json += "\"soilAO\":"      + String(finalData.soilAO) + ",";
  json += "\"uvVoltage\":"   + String(finalData.uvVoltage);
  json += "}";

  int httpCode = http.POST(json);
  
  if (httpCode > 0) {
    if (httpCode == 200 || httpCode == 201) {
      Serial.printf("Đã đẩy dữ liệu lên Render thành công! (HTTP Code: %d)\n", httpCode);
    } else {
      Serial.printf("Đã tới Render nhưng Server báo lỗi (HTTP Code: %d)\n", httpCode);
    }
  } else {
    Serial.printf("Lỗi kết nối HTTPS: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  delete client;
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_M0, OUTPUT);
  pinMode(PIN_M1, OUTPUT);
  digitalWrite(PIN_M0, LOW);
  digitalWrite(PIN_M1, LOW);

  Serial1.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  
  WiFi.mode(WIFI_STA);
  Serial.print("Đang kết nối WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected Successfully!");
  Serial.println("\n=== GATE 3 READY TO POST TO RENDER ===");
}

void loop() {
  unsigned long currentMillis = millis();

  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousWifiMillis >= wifiCheckInterval)) {
    Serial.print(millis());
    Serial.println(" Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    previousWifiMillis = currentMillis;
  }

  if (Serial1.available() >= sizeof(incomingData)) {
    Serial1.readBytes((uint8_t*)&incomingData, sizeof(incomingData));
    
    if (isnan(incomingData.temperature) || isnan(incomingData.humidity)) {
      Serial.println("Lỗi gói tin LoRa (NaN). Bỏ qua mẫu này!");
    } else {
      finalData = incomingData;
      hasNewData = true;
    }
    
    while(Serial1.available()) Serial1.read();
  }

  if (currentMillis - previousMillis >= postInterval) {
    if (WiFi.status() == WL_CONNECTED && hasNewData) {
      postToRender();
      hasNewData = false; 
    } 
    else if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Đang chờ mạng để gửi dữ liệu...");
    }
    previousMillis = currentMillis;
  }
}