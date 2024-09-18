#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "credentials.h"  // WiFi 정보가 포함된 파일

WebSocketsClient webSocket;
const char* ws_server = "192.168.75.33"; // Node.js 서버 IP
const uint16_t ws_port = 3000;           // WebSocket 포트

// 저주파 통과 필터 함수
int lowPassFilter(int16_t currentSample, int16_t previousSample, float alpha) {
  return (alpha * currentSample) + ((1.0 - alpha) * previousSample);
}

// I2S 마이크 설정 함수
void setupI2SMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 8000,  // 샘플 레이트 설정 (Google Speech-to-Text의 경우 16000 Hz 권장)
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
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  webSocket.begin(ws_server, ws_port, "/");
  webSocket.onEvent(webSocketEvent);

  setupI2SMicrophone(); // I2S 마이크 설정 함수 호출
}

void loop() {
  int16_t i2sData[512];  // I2S 데이터 배열
  size_t bytesRead = 0;
  int gain = 7;          // 신호 증폭 비율
  int threshold = 200;   // 노이즈 게이트 임계값
  int16_t previousSample = 0;
  float alpha = 0.95;    // 저주파 통과 필터의 강도 (0.95는 95% 필터링)

  // I2S에서 데이터 읽기
  esp_err_t result = i2s_read(I2S_NUM_0, (char*)i2sData, sizeof(i2sData), &bytesRead, portMAX_DELAY);

  // 데이터가 정상적으로 읽혔는지 확인
  if (result == ESP_OK && bytesRead > 0) {
    // 필터링 및 증폭된 데이터를 WebSocket을 통해 전송
    for (int i = 0; i < bytesRead / sizeof(int16_t); i++) {
      i2sData[i] = i2sData[i] * gain;  // 신호 증폭

      // 노이즈 게이트 적용 (임계값 이하 신호는 무시)
      if (abs(i2sData[i]) > threshold) {
        i2sData[i] = lowPassFilter(i2sData[i], previousSample, alpha);
        previousSample = i2sData[i];
      } else {
        i2sData[i] = 0; // 임계값 이하인 경우 무시
      }
    }

    // WebSocket을 통해 I2S 데이터를 전송
    webSocket.sendBIN((uint8_t*)i2sData, bytesRead);

    // 전송된 데이터 로그 출력
    Serial.println("Sent data to WebSocket:");
    for (int i = 0; i < bytesRead / sizeof(int16_t); i++) {
      Serial.print(i2sData[i]);
      Serial.print(" ");
    }
    Serial.println(); // 한 줄 띄우기
  }

  webSocket.loop(); // WebSocket 연결 유지
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
    case WStype_ERROR:
      Serial.println("WebSocket error");
      break;
  }
}
