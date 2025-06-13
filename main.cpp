#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>

// --- CONFIGURAÇÃO DO DHT22 ---
#define DHTPIN   14
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- CONFIGURAÇÃO DO LDR ---
#define LDR_PIN 35

// --- CREDENCIAIS WIFI ---
const char* ssid     = "Wokwi-GUEST";
const char* password = "";

// --- NTP ---
const long  gmtOffsetSec      = -3 * 3600;
const int   daylightOffsetSec = 0;
const char* ntpServer         = "pool.ntp.org";

// --- DELAYS DAS TASKS ---
const TickType_t measureInterval = pdMS_TO_TICKS(5000);
const TickType_t sendInterval    = pdMS_TO_TICKS(10000);

// --- ESTRUTURA DE MEDIDA ---
typedef struct {
  float temperature;   // 4 bytes
  float humidity;      // 4 bytes
  int   light;         // 4 bytes
  char  timestamp[25];// 25 bytes
} Measurement_t;

// --- FILA ---
QueueHandle_t measureQueue;

void TaskCollector(void* pvParameters) {
  for (;;) {
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    int raw    = analogRead(LDR_PIN);

    float voltage = raw / 4096. * 3.3;
    float resistance = 2000 * voltage / (1 - voltage / 3.3);
    float lux = pow(33 * 1e3 * pow(10, 0.7) / resistance, (1 / 0.7));



    if (!isnan(temp) && !isnan(hum)) {
      Measurement_t m;
      m.temperature = temp;
      m.humidity    = hum;
      m.light       = lux;

      // Timestamp ISO 8601 UTC
      time_t now;
      struct tm timeinfo;
      time(&now);
      gmtime_r(&now, &timeinfo);
      strftime(m.timestamp, sizeof(m.timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

      // Enfileira a medida
      xQueueSend(measureQueue, &m, 0);
    }
    vTaskDelay(measureInterval);
  }
}

void TaskSender(void* pvParameters) {
  for (;;) {
    // Aguarda intervalo de envio
    vTaskDelay(sendInterval);

    // Monta payload com todas as leituras acumuladas
    Measurement_t m;
    String payload = "[";
    bool first = true;

    while (xQueueReceive(measureQueue, &m, 0) == pdTRUE) {
      if (!first) payload += ",";
      payload += "{";
      payload += "\"temp\":"     + String(m.temperature, 1) + ",";
      payload += "\"umidade\":"  + String(m.humidity, 0)    + ",";
      payload += "\"light\":"    + String(m.light)          + ",";
      payload += "\"timestamp\":\"" + String(m.timestamp)  + "\"";
      payload += "}";
      first = false;
    }
    payload += "]";

    // Print no console ao enviar
    Serial.println("=== Enviando payload ===");
    Serial.println(payload);

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin("https://clima-backend-xi.vercel.app/api/dados");
      http.addHeader("Content-Type", "application/json");
      int httpCode = http.POST(payload);
      Serial.printf("HTTP POST code: %d\n", httpCode);
      http.end();
    } else {
      Serial.println("WiFi não conectado. Payload não enviado.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  // Conecta ao WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK!");

  // Sincroniza NTP
  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
  Serial.print("Sincronizando NTP");
  time_t now = time(nullptr);
  while (now < (2025 - 1970) * 31536000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" OK!");

  // Cria fila de medidas
  measureQueue = xQueueCreate(10, sizeof(Measurement_t));

  // Cria tasks em cores separadas
  xTaskCreatePinnedToCore(TaskCollector, "Collector", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskSender,    "Sender",    4096, NULL, 1, NULL, 0);
}

void loop() {
  // Nada aqui: tudo é feito nas tasks
}
