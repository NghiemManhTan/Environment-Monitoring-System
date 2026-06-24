#include <esp_now.h>
#include <WiFi.h>
#define RXp2 16
#define TXp2 17
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

void OnDataRecv(const uint8_t * mac, const uint8_t *incoming, int len) {
  memcpy(&incomingData, incoming, sizeof(incomingData));
  
  Serial.println("\n[GATE 2] Đã nhận dữ liệu từ Gate 1 (ESP-NOW)");
  Serial.printf("Temp: %.2f C | Hum: %.2f %%\n", incomingData.temperature, incomingData.humidity);
  
  Serial2.write((uint8_t*)&incomingData, sizeof(incomingData));
  
  Serial.println("[GATE 2] Đã chuyển tiếp thành công qua LoRa tới Gate 3!");
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_M0, OUTPUT);
  pinMode(PIN_M1, OUTPUT);
  digitalWrite(PIN_M0, LOW);
  digitalWrite(PIN_M1, LOW);

  Serial2.begin(9600, SERIAL_8N1, RXp2, TXp2);
  
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Lỗi khởi tạo ESP-NOW!");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("\n=== GATE 2 (BRIDGE) ĐÃ SẴN SÀNG ===");
  Serial.println("Đang lắng nghe ESP-NOW và tự động Forward qua LoRa UART...");
}

void loop() {
}