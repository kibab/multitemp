#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <driver/adc.h>
#include <esp_wifi.h>
#include <esp_bt.h>

#include "wifi_settings.h"

#define PUSH_INTERVAL_SEC 120
#define MAX_CONN_ATTEMPTS 10
#define MAX_PUSH_RETRIES 3

// GPIO where the DS18B20 is connected to
const int oneWireBus = 21;     

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

/* Counter of failed WiFi connect attempts. Preserved during deep sleep. */
RTC_DATA_ATTR unsigned int total_conn_failures = 0;
float temp;

/* Generate metrics in Prometheus format */
String getMetrics() {
  return String(
      "# HELP temperature Ambient temperature.\n" 
      "# TYPE temperature gauge\n"
      "temperature " + String(temp) + "\n"
      "# HELP conn_failures_count Number of failed WiFi connect attempts\n" + 
      "# TYPE conn_failures_count counter\n" +
      "conn_failures_count " + String(total_conn_failures) + "\n"
    );
}

/* Push metrics to PushGateway endpoint */
bool pushMetrics() {
  bool success = false;
  Serial.println("Starting to push metrics to " PUSH_URL);
  
  HTTPClient http;

  http.begin(PUSH_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpResponseCode = http.POST(getMetrics());
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);
  if (httpResponseCode > 0)
    success = true;
  // Free resources
  http.end();
  return success;
}

/* Push metrics and retry if not successful */
void pushMetricsRetry() {
  for (int i = 1; i < MAX_PUSH_RETRIES; i++) {
    if (pushMetrics())
      break;
  }
}

bool connectNetwork() {
  WiFi.mode(WIFI_STA);

  Serial.println("Starting to connect to the WiFi network");
  Serial.printf("Prev conn failures: %d\n", total_conn_failures);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait for connection
  int conn_attempts_left = MAX_CONN_ATTEMPTS;
  while (WiFi.status() != WL_CONNECTED && conn_attempts_left > 0) {
    delay(500);
    Serial.print(".");
    conn_attempts_left--;
  }
  if (conn_attempts_left == 0) {
    Serial.println("\nFailed to connect to WiFi!");
    total_conn_failures++;
    return false;
  }
  Serial.println("");
  Serial.println("Connected to " WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  
  return true;
}

/* Stop all power-hungry h/w, schedule time to wake up, and hibernate */
void goSleep(int wakeupTimer) {
  Serial.println("Going to sleep...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  esp_wifi_stop();
  esp_bt_controller_disable();

  // Configure the timer to wake us up!
  esp_sleep_enable_timer_wakeup(wakeupTimer * 1000000L);

  // Go to sleep! Zzzz
  esp_deep_sleep_start();
}

void updateSensors() {
  sensors.requestTemperatures(); 
  temp = sensors.getTempCByIndex(0);
  Serial.printf("Temperature: %.01f ºC\n", temp);
}

void setupHardware() {
  setCpuFrequencyMhz(80); // 80 Mhz is minimum for WiFi to work
  // Start the Serial Monitor
  Serial.begin(1000000);
  // Start the DS18B20 sensor
  sensors.begin();  
}

void setup() {

  setupHardware();
  bool wifi_connected = connectNetwork();
  if (!wifi_connected) {
    goSleep(PUSH_INTERVAL_SEC);
    /* NOTREACHED */
  }

  updateSensors();
  pushMetricsRetry();
  goSleep(PUSH_INTERVAL_SEC);
  /* NOTREACHED */
}

void loop() {
  /* 
    *  loop() will be never called because the code in setup() goes into deep sleep.
    *  The original code is at https://randomnerdtutorials.com/esp32-ds18b20-temperature-arduino-ide/
    */
  /* NOTREACHED */
}
