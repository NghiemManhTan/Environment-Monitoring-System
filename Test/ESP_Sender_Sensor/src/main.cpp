#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>

#define DHTPIN 5
#define DHTTYPE DHT22
#define RAIN_DO 6
#define RAIN_AO 1
#define SOIL_DO 14
#define SOIL_AO 16
#define UV_PIN 17

DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;

uint8_t broadcastAddress[] = {0x88, 0x57, 0x21, 0xB2, 0xCB, 0xAC};

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

struct_message myData;
esp_now_peer_info_t peerInfo;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nTrạng thái gửi gói tin: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Thành công" : "Thất bại");
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(RAIN_DO, INPUT);
  pinMode(SOIL_DO, INPUT);

  Wire.begin(42, 45); 

  if (lightMeter.begin()) {
    Serial.println("BH1750 OK");
  } else {
    Serial.println("BH1750 FAIL");
  }

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Lỗi khởi tạo ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Không thể thêm thiết bị nhận");
    return;
  }

  Serial.println("Mạch gửi sẵn sàng");
}

void loop() {
  Serial.println("\n==Thông tin cảm biến==");

  myData.humidity = dht.readHumidity();
  myData.temperature = dht.readTemperature();
  myData.lux = lightMeter.readLightLevel();
  myData.rainDO = digitalRead(RAIN_DO);
  myData.rainAO = analogRead(RAIN_AO);
  myData.soilDO = digitalRead(SOIL_DO);
  myData.soilAO = analogRead(SOIL_AO);
  
  int uvRaw = analogRead(UV_PIN);
  myData.uvVoltage = uvRaw * 3.3 / 4095.0;
  
  //kiểm tra tin hiệu sensors (xóa sau khi kiểm tra)
  Serial.printf("Nhiệt độ: %.2f C | Độ ẩm: %.2f %%\n", myData.temperature, myData.humidity);
  Serial.printf("Ánh sáng: %.2f lux\n", myData.lux);
  Serial.printf("Cảm biến Mưa (AO): %d | Đất (AO): %d\n", myData.rainAO, myData.soilAO);
  Serial.printf("Điện áp UV: %.2f V\n", myData.uvVoltage);
  
  if (isnan(myData.temperature) || isnan(myData.humidity)) {
    Serial.println("Lỗi đọc cảm biến DHT!");
  } else {
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    if (result == ESP_OK) {
      Serial.println("Đã gửi dữ liệu");
    } else {
      Serial.println("Lỗi khi gửi dữ liệu");
    }
  }

  delay(2000);
}