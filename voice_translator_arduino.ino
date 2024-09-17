#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// WiFi 설정
const char* ssid = "SK_WiFiGIGA97BE_2.4G";  // 자신의 WiFi SSID로 변경
const char* password = "AWKB0@6876";        // 자신의 WiFi 패스워드로 변경
const char* serverUrl = "http://192.168.75.215:3000/audio"; // Node.js 서버 IP와 포트

// 저주파 통과 필터 함수
int lowPassFilter(int16_t currentSample, int16_t previousSample, float alpha) {
  return (alpha * currentSample) + ((1.0 - alpha) * previousSample);
}

// I2S 마이크 설정 함수
void setupI2SMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = 14,   // I2S SCK 핀 설정
    .ws_io_num = 15,    // I2S WS 핀 설정
    .data_out_num = -1, // 데이터 출력 없음
    .data_in_num = 12   // I2S 데이터 입력 핀 설정 (INMP441 마이크)
  };

  // I2S 드라이버 설치 및 핀 설정
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void setup() {
  Serial.begin(115200);

  // WiFi 연결
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  setupI2SMicrophone();
}

void loop() {
  int16_t i2sData[512];  // 데이터를 저장할 배열
  size_t bytesRead = 0;
  int gain = 7;          // 신호 증폭 비율
  int threshold = 200;   // 노이즈 게이트 임계값
  int16_t previousSample = 0;
  float alpha = 0.95;    // 저주파 통과 필터의 강도 (0.95는 95% 필터링)

  // I2S에서 데이터 읽기
  esp_err_t result = i2s_read(I2S_NUM_0, (char*)i2sData, sizeof(i2sData), &bytesRead, portMAX_DELAY);

  // 데이터가 정상적으로 읽혔는지 확인
  if (result == ESP_OK && bytesRead > 0) {
    HTTPClient http;
    http.begin(serverUrl); // 서버 URL 설정
    http.addHeader("Content-Type", "application/json"); // JSON 형식으로 데이터 전송

    for (int i = 0; i < bytesRead / sizeof(int16_t); i++) {
      i2sData[i] = i2sData[i] * gain;  // 신호 증폭

      // 노이즈 게이트 적용 (임계값 이하 신호는 무시)
      if (abs(i2sData[i]) > threshold) {
        // 저주파 통과 필터 적용
        i2sData[i] = lowPassFilter(i2sData[i], previousSample, alpha);
        previousSample = i2sData[i];

        // 시리얼 플로터로 출력
        Serial.println(i2sData[i]);

        // 서버로 데이터 전송
        String postData = "{\"audio\": " + String(i2sData[i]) + "}";
        int httpResponseCode = http.POST(postData);

        // 서버 응답 확인
        if (httpResponseCode > 0) {
          String response = http.getString();
          Serial.println("Server response: " + response);
        } else {
          Serial.println("Error on sending POST: " + String(httpResponseCode));
        }
      }
    }
    http.end(); // HTTP 연결 종료
  }

  // delay()를 제거하여 실시간 처리가 가능하도록 함
}
