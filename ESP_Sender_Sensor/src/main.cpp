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

unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;
const long READ_INTERVAL = 200;
const long SEND_INTERVAL = 2000;

float tempArr[15], humArr[15], luxArr[15], uvArr[15];
int rainAoArr[15], soilAoArr[15];
int sumRainDO = 0, sumSoilDO = 0;
int readCount = 0; 

template <typename T>
void sortArray(T arr[], int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      if (arr[j] > arr[j + 1]) {
        T temp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = temp;
      }
    }
  }
}

template <typename T>
float getTrimmedMean(T arr[], int count) {
  if (count == 0) return 0;
  if (count <= 2) {
    float sum = 0;
    for (int i = 0; i < count; i++) sum += arr[i];
    return sum / count;
  }
  
  sortArray(arr, count);
  
  int trim = count * 0.2; 
  float sum = 0;
  for (int i = trim; i < count - trim; i++) {
    sum += arr[i];
  }
  
  return sum / (count - 2 * trim);
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success ✅" : "Failed ❌");
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
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("\n=== GATE 1 READY (SMART FILTERING MODE) ===");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastReadTime >= READ_INTERVAL) {
    lastReadTime = currentMillis;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("DHT22 read error, skipping this sample.");
    } else {
      if (readCount < 15) {
        tempArr[readCount] = t;
        humArr[readCount] = h;
        luxArr[readCount] = lightMeter.readLightLevel();
        rainAoArr[readCount] = analogRead(RAIN_AO);
        soilAoArr[readCount] = analogRead(SOIL_AO);
        
        int uvRaw = analogRead(UV_PIN);
        uvArr[readCount] = (uvRaw * 3.3 / 4095.0);
        
        sumRainDO += digitalRead(RAIN_DO);
        sumSoilDO += digitalRead(SOIL_DO);

        readCount++;
        Serial.printf("Collected raw sample #%d...\n", readCount);
      }
    }
  }

  if (currentMillis - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentMillis;

    if (readCount > 0) {
      // Đưa các mảng qua hàm lọc Trimmed Mean để lấy dữ liệu lõi tinh khiết
      myData.temperature = getTrimmedMean(tempArr, readCount);
      myData.humidity = getTrimmedMean(humArr, readCount);
      myData.lux = getTrimmedMean(luxArr, readCount);
      myData.rainAO = (int)getTrimmedMean(rainAoArr, readCount);
      myData.soilAO = (int)getTrimmedMean(soilAoArr, readCount);
      myData.uvVoltage = getTrimmedMean(uvArr, readCount);

      myData.rainDO = (sumRainDO * 2 >= readCount) ? 1 : 0;
      myData.soilDO = (sumSoilDO * 2 >= readCount) ? 1 : 0;

      Serial.println("\n=== PACKAGING AND SENDING CLEAN DATA ===");
      Serial.printf("Clean Temp: %.2f C | Hum: %.2f %% (Trimmed from %d samples)\n", myData.temperature, myData.humidity, readCount);
      Serial.printf("Clean RainAO: %d | SoilAO: %d\n", myData.rainAO, myData.soilAO);

      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

      sumRainDO = 0; 
      sumSoilDO = 0;
      readCount = 0;
    } else {
      Serial.println("\nNo valid data samples to send in this cycle!");
    }
  }
}