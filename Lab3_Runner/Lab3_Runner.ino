// Lab 3 - De 1: ESP32 + BMP180 + FreeRTOS + MQTT
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <PubSubClient.h>
#include <esp_arduino_version.h>

#define TOPIC "lab3/runner/bxh-e42a"

constexpr uint8_t LED_PIN = 26, BUZZER_PIN = 25;
constexpr uint8_t SDA_PIN = 21, SCL_PIN = 22;
constexpr uint32_t SAMPLE_MS = 1000, RETRY_MS = 5000;
constexpr float SEA_LEVEL_HPA = 1013.25f, ALT_CHANGE_M = 5.0f;
constexpr uint8_t ALT_SAMPLES = 6;

struct Reading {
  float temperature;
  float pressure;
  float altitude;
};

struct AlertSample {
  float temperature;
  float altitude;
};

enum class Target : uint8_t { Mode, Led, Buzzer };
struct Command { Target target; bool enabled; };
struct OutputState { bool manual; bool led; bool buzzer; uint8_t alert; };

QueueHandle_t data_queue, data_alert, commands, output_state;
WiFiClient transport;
PubSubClient mqtt(transport);

void read_task(void*) {
  Serial.printf("[Read] Core %d\n", xPortGetCoreID());
  Adafruit_BMP085 bmp;
  Wire.begin(SDA_PIN, SCL_PIN);
  while (!bmp.begin()) {
    Serial.println("[BMP180] Khong tim thay cam bien");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  TickType_t next_sample = xTaskGetTickCount();
  for (;;) {
    const float temperature = bmp.readTemperature();
    const float pressure = bmp.readPressure() / 100.0f;
    if (isfinite(temperature) && isfinite(pressure) && pressure > 0) {
      const float altitude = 44330.0f * (1.0f - powf(pressure / SEA_LEVEL_HPA, 1.0f / 5.255f));
      const Reading reading{temperature, pressure, altitude};
      const AlertSample sample{temperature, altitude};
      Serial.printf("[Read] T=%.1f C | P=%.2f hPa | h=%.1f m\n", temperature, pressure, altitude);
      xQueueOverwrite(data_queue, &reading);
      xQueueOverwrite(data_alert, &sample);
    }
    vTaskDelayUntil(&next_sample, pdMS_TO_TICKS(SAMPLE_MS));
  }
}

void alert_task(void*) {
  Serial.printf("[Alert] Core %d\n", xPortGetCoreID());
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  const bool buzzer_ready = ledcAttach(BUZZER_PIN, 2000, 10);
  constexpr uint8_t buzzer_output = BUZZER_PIN;
#else
  constexpr uint8_t buzzer_output = 0;
  const bool buzzer_ready = ledcSetup(buzzer_output, 2000, 10) > 0;
  ledcAttachPin(BUZZER_PIN, buzzer_output);
#endif
  if (!buzzer_ready) Serial.println("[Alert] Loi khoi tao PWM");

  bool manual = false, manual_led = false, manual_buzzer = false;
  bool hot = false, cold = false, altitude_alert = false;
  float history[ALT_SAMPLES]{};
  uint8_t position = 0, count = 0;
  OutputState previous{};
  bool first = true;
  uint32_t last_sample = 0, previous_tone = 0;
  for (;;) {
    Command command;
    while (xQueueReceive(commands, &command, 0) == pdTRUE) {
      switch (command.target) {
        case Target::Mode:
          manual = command.enabled;
          manual_led = manual_buzzer = false;
          break;
        case Target::Led:
          manual = true;
          manual_led = command.enabled;
          break;
        case Target::Buzzer:
          manual = true;
          manual_buzzer = command.enabled;
          break;
      }
    }

    const uint32_t now = millis();
    AlertSample sample;
    if (xQueueReceive(data_alert, &sample, 0) == pdTRUE) {
      if (now - last_sample > 2 * SAMPLE_MS) position = count = 0;
      last_sample = now;
      hot = sample.temperature >= (hot ? 33.0f : 35.0f);
      cold = sample.temperature <= (cold ? 12.0f : 10.0f);
      history[position] = sample.altitude;
      position = (position + 1) % ALT_SAMPLES;
      if (count < ALT_SAMPLES) ++count;
      altitude_alert = count == ALT_SAMPLES && fabsf(sample.altitude - history[position]) >= ALT_CHANGE_M;
    }

    const uint8_t alert = altitude_alert ? 3 : hot ? 1 : cold ? 2 : 0;
    const bool led = manual ? manual_led : alert != 0;
    const uint32_t tone = (buzzer_ready && (manual ? manual_buzzer : alert != 0))
                            ? (manual || alert != 3 ? 2000 : 3000) : 0;
    if (first || led != previous.led) digitalWrite(LED_PIN, led);
    if (first || tone != previous_tone) {
      ledcWriteTone(buzzer_output, tone);
      previous_tone = tone;
    }

    const OutputState state{manual, led, tone != 0, alert};
    if (first || state.alert != previous.alert || state.manual != previous.manual ||
        state.led != previous.led || state.buzzer != previous.buzzer) {
      Serial.printf("[Alert] code=%u | %s | LED=%d | Buzzer=%d\n",
                    alert, manual ? "MANUAL" : "AUTO", state.led, state.buzzer);
      xQueueOverwrite(output_state, &state);
      previous = state;
      first = false;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void on_mqtt(char* topic, byte* payload, unsigned int size) {
  Command command;
  if (strcmp(topic, TOPIC "/mode/set") == 0) {
    if (size == 6 && memcmp(payload, "manual", 6) == 0) command = {Target::Mode, true};
    else if (size == 4 && memcmp(payload, "auto", 4) == 0) command = {Target::Mode, false};
    else return;
  } else {
    if (size != 1 || (payload[0] != '0' && payload[0] != '1')) return;
    if (strcmp(topic, TOPIC "/led/set") == 0) command = {Target::Led, payload[0] == '1'};
    else if (strcmp(topic, TOPIC "/buzzer/set") == 0) command = {Target::Buzzer, payload[0] == '1'};
    else return;
  }
  if (xQueueSend(commands, &command, 0) != pdTRUE) Serial.println("[MQTT] Hang doi lenh day");
}

void publish_reading(const Reading& reading) {
  char value[24];
  snprintf(value, sizeof(value), "%.1f", reading.temperature);
  mqtt.publish(TOPIC "/temp", value, true);
  snprintf(value, sizeof(value), "%.2f", reading.pressure);
  mqtt.publish(TOPIC "/press", value, true);
  snprintf(value, sizeof(value), "%.1f", reading.altitude);
  mqtt.publish(TOPIC "/alt", value, true);
}

void send_task(void*) {
  Serial.printf("[Send] Core %d\n", xPortGetCoreID());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin("Wokwi-GUEST", "", 6);
  mqtt.setServer("test.mosquitto.org", 1883);
  mqtt.setSocketTimeout(3);
  mqtt.setCallback(on_mqtt);

  char client_id[32];
  snprintf(client_id, sizeof(client_id), "lab3-bxh-%08lx", (unsigned long)esp_random());
  uint32_t last_attempt = millis() - RETRY_MS;
  bool sent = false;
  OutputState previous{};
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (mqtt.connected()) transport.stop();
    } else {
      const uint32_t now = millis();
      if (!mqtt.connected() && now - last_attempt >= RETRY_MS) {
        last_attempt = now;
        if (mqtt.connect(client_id, TOPIC "/status", 0, true, "offline")) {
          Serial.println("[MQTT] Da ket noi");
          mqtt.subscribe(TOPIC "/+/set");
          sent = false;
          mqtt.publish(TOPIC "/status", "online", true);
        } else {
          Serial.printf("[MQTT] Ket noi loi %d\n", mqtt.state());
        }
      }

      if (mqtt.loop()) {
        Reading reading;
        if (xQueueReceive(data_queue, &reading, 0) == pdTRUE) publish_reading(reading);
        // Keep the latest state available for reconnect. Republish on changes.
        OutputState state;
        if (xQueuePeek(output_state, &state, 0) == pdTRUE &&
            (!sent || state.alert != previous.alert || state.manual != previous.manual ||
             state.led != previous.led || state.buzzer != previous.buzzer)) {
          char code[4];
          snprintf(code, sizeof(code), "%u", state.alert);
          const bool ok = mqtt.publish(TOPIC "/alert", code, true) &&
                          mqtt.publish(TOPIC "/mode/state", state.manual ? "manual" : "auto", true) &&
                          mqtt.publish(TOPIC "/led/state", state.led ? "1" : "0", true) &&
                          mqtt.publish(TOPIC "/buzzer/state", state.buzzer ? "1" : "0", true);
          if (ok) {
            previous = state;
            sent = true;
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void setup() {
  Serial.begin(115200);
  data_queue = xQueueCreate(1, sizeof(Reading));
  data_alert = xQueueCreate(1, sizeof(AlertSample));
  commands = xQueueCreate(8, sizeof(Command));
  output_state = xQueueCreate(1, sizeof(OutputState));

  if (!data_queue || !data_alert || !commands || !output_state) {
    Serial.println("[RTOS] Khong tao duoc queue");
    return;
  }
  if (xTaskCreatePinnedToCore(alert_task, "Alert", 3072, nullptr, 2, nullptr, 1) != pdPASS ||
      xTaskCreatePinnedToCore(read_task, "Read", 4096, nullptr, 1, nullptr, 1) != pdPASS ||
      xTaskCreatePinnedToCore(send_task, "Send", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("[RTOS] Khong tao duoc task");
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

