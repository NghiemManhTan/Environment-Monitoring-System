#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "Just T4n";
const char* password = "12348765";

String serverName = "http://10.116.67.192:5000/api/sensor"; // Địa chỉ IPv4 của Laptop

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

struct_message sensorData;
bool isDataReady = false; 

unsigned long previousMillis = 0;
const long postInterval = 2000; 

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&sensorData, incomingData, sizeof(sensorData));
  isDataReady = true;
  
  Serial.println("\n=== SENSOR DATA RECEIVED ===");
  Serial.printf("Temperature: %.2f C | Humidity: %.2f %%\n", sensorData.temperature, sensorData.humidity);
  Serial.printf("Light: %.2f lux\n", sensorData.lux);
  Serial.printf("Rain (AO): %d | Soil (AO): %d\n", sensorData.rainAO, sensorData.soilAO);
  Serial.printf("UV Voltage: %.2f V\n", sensorData.uvVoltage);
  Serial.println("----------------------------");
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected Successfully!");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Initialization Failed!");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Gateway Ready! Listening for ESP-NOW...");
}

void loop() {
  if ((millis() - previousMillis) > postInterval) {
    if (WiFi.status() == WL_CONNECTED && isDataReady) {
      
      HTTPClient http;
      http.begin(serverName);

      http.addHeader("Content-Type", "application/json");
      
      String jsonPayload = "{\"temperature\":" + String(sensorData.temperature) + 
                           ",\"humidity\":" + String(sensorData.humidity) + 
                           ",\"lux\":" + String(sensorData.lux) + 
                           ",\"rainAO\":" + String(sensorData.rainAO) + 
                           ",\"soilAO\":" + String(sensorData.soilAO) + 
                           ",\"uvVoltage\":" + String(sensorData.uvVoltage) + "}";

      Serial.print("Pushing data to Local Server... ");
      
      // Dùng lệnh POST thay vì GET
      int httpResponseCode = http.POST(jsonPayload);

      if (httpResponseCode > 0) {
        Serial.printf("SUCCESS! (Code: %d)\n", httpResponseCode);
      } else {
        Serial.printf("FAILED! HTTP Code: %s\n", http.errorToString(httpResponseCode).c_str());
      }
      
      http.end();

      previousMillis = millis();
      isDataReady = false; 
    }
  }
}