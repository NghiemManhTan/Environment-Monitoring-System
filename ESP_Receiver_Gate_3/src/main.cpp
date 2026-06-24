#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "VGU_Student_Guest";
const char* password = "";
String dashboardUrl = "https://iot-env-monitor.onrender.com/data/sensor";

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

// --- BIẾN PHỤC VỤ EMA (BỘ LỌC LÀM MƯỢT) ---
float emaTemp = 0, emaHum = 0, emaLux = 0, emaUvVoltage = 0;
float emaRainAO = 0, emaSoilAO = 0;
int latestRainDO = 0, latestSoilDO = 0; // Digital chỉ lấy 0 hoặc 1

const float ALPHA = 0.15;  // Hệ số mượt (Càng nhỏ biểu đồ càng cong mượt)
bool isFirstSample = true; // Cờ đánh dấu mẫu dữ liệu đầu tiên khởi động hệ thống
int sampleCount = 0;

unsigned long previousMillis = 0;
const long postInterval = 2000; 

void postToDashboard() {
  HTTPClient http;
  http.setTimeout(15000); 
  http.begin(dashboardUrl);
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

  int code = http.POST(json);
  if (code > 0) {
    Serial.printf("✅ Đã đẩy lên Web thành công! (HTTP Code: %d)\n", code);
  } else {
    Serial.printf("❌ Lỗi khi gửi dữ liệu lên Web: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_M0, OUTPUT);
  pinMode(PIN_M1, OUTPUT);
  digitalWrite(PIN_M0, LOW);
  digitalWrite(PIN_M1, LOW);

  Serial1.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected Successfully!");
  Serial.println("\n=== GATE 3 (MASTER) ĐÃ SẴN SÀNG ===");
}

void loop() {
  // 1. LẮNG NGHE LORA UART
  if (Serial1.available() >= sizeof(incomingData)) {
    Serial1.readBytes((uint8_t*)&incomingData, sizeof(incomingData));
    
    // --- BƯỚC 1: CLEAN DATA & APPLY EMA ---
    if (isnan(incomingData.temperature) || isnan(incomingData.humidity) ||
        incomingData.temperature < -10.0 || incomingData.temperature > 80.0 ||
        incomingData.humidity < 0.0 || incomingData.humidity > 100.0) {
      Serial.println("Lọc: Dữ liệu lỗi (NaN hoặc Outlier). Bỏ qua mẫu này!");
    } else {
      
      // Khởi tạo điểm neo đầu tiên cho bộ lọc
      if (isFirstSample) {
        emaTemp = incomingData.temperature;
        emaHum = incomingData.humidity;
        emaLux = incomingData.lux;
        emaRainAO = incomingData.rainAO;
        emaSoilAO = incomingData.soilAO;
        emaUvVoltage = incomingData.uvVoltage;
        isFirstSample = false;
      } 
      // Kéo mượt các mẫu tiếp theo
      else {
        emaTemp = (ALPHA * incomingData.temperature) + ((1.0 - ALPHA) * emaTemp);
        emaHum = (ALPHA * incomingData.humidity) + ((1.0 - ALPHA) * emaHum);
        emaLux = (ALPHA * incomingData.lux) + ((1.0 - ALPHA) * emaLux);
        emaRainAO = (ALPHA * incomingData.rainAO) + ((1.0 - ALPHA) * emaRainAO);
        emaSoilAO = (ALPHA * incomingData.soilAO) + ((1.0 - ALPHA) * emaSoilAO);
        emaUvVoltage = (ALPHA * incomingData.uvVoltage) + ((1.0 - ALPHA) * emaUvVoltage);
      }
      
      // Tín hiệu Digital (0/1) không cần mượt, lấy luôn giá trị mới nhất
      latestRainDO = incomingData.rainDO;
      latestSoilDO = incomingData.soilDO;
      
      sampleCount++;
      Serial.printf("📡 Nhận LORA: Đã áp dụng bộ lọc EMA cho mẫu %d.\n", sampleCount);
    }
    
    // Xóa buffer thừa
    while(Serial1.available()) Serial1.read();
  }

  // 2. KỂM TRA THỜI GIAN VÀ ĐẨY LÊN WEB (Mỗi 2 giây)
  if ((millis() - previousMillis) > postInterval) {
    // Không cần sampleCount > 0 nếu bạn muốn biểu đồ web tự trôi liên tục.
    // Nhưng để tránh gọi API rác khi LoRa tắt, ta vẫn kiểm tra có nhận được tín hiệu.
    if (WiFi.status() == WL_CONNECTED && !isFirstSample && sampleCount > 0) {
      
      // --- BƯỚC 2: GÁN DỮ LIỆU ĐÃ LỌC ---
      finalData.temperature = emaTemp;
      finalData.humidity = emaHum;
      finalData.lux = emaLux;
      finalData.rainAO = (int)emaRainAO; // Ép về số nguyên
      finalData.soilAO = (int)emaSoilAO; 
      finalData.uvVoltage = emaUvVoltage;
      finalData.rainDO = latestRainDO;
      finalData.soilDO = latestSoilDO;

      Serial.println("\n=== 🚀 ĐANG XỬ LÝ & ĐẨY LÊN DASHBOARD ===");
      Serial.printf("Temp: %.2f C | Hum: %.2f %%\n", finalData.temperature, finalData.humidity);
      Serial.printf("RainAO (Đã lọc): %d | Khác biệt so với Raw: %d\n", 
                    finalData.rainAO, incomingData.rainAO - finalData.rainAO);

      // Bơm thẳng lên Server của bạn
      postToDashboard();

      // Chỉ reset sampleCount để đếm nhịp mạng, TUYỆT ĐỐI KHÔNG reset các biến emaX
      sampleCount = 0; 
      previousMillis = millis();
    } 
    else if (sampleCount == 0 && !isFirstSample) {
      Serial.println("⏳ Đang chờ sóng LoRa...");
      previousMillis = millis();
    }
  }
}