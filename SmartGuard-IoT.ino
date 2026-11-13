#include <WiFi.h>
#include <PubSubClient.h>

// =====================================================
// 1. WIFI CONFIGURATION
// =====================================================

const char* WIFI_SSID = "Prashu";
const char* WIFI_PASSWORD = "Prashu2004";

// =====================================================
// 2. MQTT CONFIGURATION
// =====================================================

const char* MQTT_SERVER = "broker.emqx.io";
const int MQTT_PORT = 1883;

const char* BASE_TOPIC = "smartguard/device01";

WiFiClient espClient;
PubSubClient mqtt(espClient);

String telemetryTopic = String(BASE_TOPIC) + "/telemetry";
String eventTopic = String(BASE_TOPIC) + "/event";
String statusTopic = String(BASE_TOPIC) + "/status";

String ledCommandTopic = String(BASE_TOPIC) + "/cmd/led";
String armedCommandTopic = String(BASE_TOPIC) + "/cmd/armed";

// =====================================================
// 3. GPIO PIN CONFIGURATION
// =====================================================

#define MQ_PIN       34
#define PIR_PIN      27
#define IR_PIN       25

#define GREEN_LED    18
#define YELLOW_LED   19
#define RED_LED      21

// Change to HIGH if your IR module has opposite logic
#define IR_DETECTED_LEVEL LOW

// =====================================================
// 4. PROJECT SETTINGS
// =====================================================

// Provisional threshold; calibrate using stable baseline readings
const int MQ_THRESHOLD = 1800;

bool systemArmed = false;

// Manual green LED control
bool remoteLedOn = false;
bool manualLedOverride = false;

// Previous sensor states for event detection
bool previousGasAlert = false;
bool previousMotion = false;
bool previousObstacle = false;

// Timing variables
unsigned long lastTelemetry = 0;
unsigned long lastSerialPrint = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

const unsigned long TELEMETRY_INTERVAL = 2000;
const unsigned long SERIAL_INTERVAL = 1000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long MQTT_RETRY_INTERVAL = 3000;

// =====================================================
// 5. EVENT PUBLISHING
// =====================================================

void publishEvent(const char* eventName) {

  if (!mqtt.connected()) return;

  mqtt.publish(eventTopic.c_str(), eventName);

  Serial.print("EVENT: ");
  Serial.println(eventName);
}

// =====================================================
// 6. LED CONTROL
// =====================================================

void updateLEDs(bool gasAlert,
                bool motion,
                bool obstacle) {

  // A security detection affects the green safe indicator
  // only when the system is armed.
  bool securityAlert =
      systemArmed && (motion || obstacle);

  bool normalStatus =
      !gasAlert && !securityAlert;

  // Manual control overrides automatic green LED behavior
  bool greenState;

  if (manualLedOverride) {
    greenState = remoteLedOn;
  } else {
    greenState = normalStatus;
  }

  digitalWrite(GREEN_LED, greenState ? HIGH : LOW);

  // Yellow LED: armed security detection
  digitalWrite(YELLOW_LED,
               securityAlert ? HIGH : LOW);

  // Red LED: gas warning
  digitalWrite(RED_LED,
               gasAlert ? HIGH : LOW);
}

// =====================================================
// 7. MQTT CALLBACK
// =====================================================

void mqttCallback(char* topic,
                  byte* payload,
                  unsigned int length) {

  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  message.trim();

  String receivedTopic = String(topic);

  Serial.print("Received command: ");
  Serial.println(message);

  // ---------------------------------------------------
  // REMOTE GREEN LED CONTROL
  // ---------------------------------------------------

  if (receivedTopic == ledCommandTopic) {

    if (message.equalsIgnoreCase("ON")) {

      remoteLedOn = true;
      manualLedOverride = true;

      Serial.println("Manual LED: ON");
    }

    else if (message.equalsIgnoreCase("OFF")) {

      remoteLedOn = false;
      manualLedOverride = true;

      Serial.println("Manual LED: OFF");
    }

    else if (message.equalsIgnoreCase("AUTO")) {

      manualLedOverride = false;

      Serial.println("LED: AUTOMATIC MODE");
    }
  }

  // ---------------------------------------------------
  // SECURITY ARM / DISARM CONTROL
  // ---------------------------------------------------

  else if (receivedTopic == armedCommandTopic) {

    if (message.equalsIgnoreCase("ARM")) {

      if (!systemArmed) {

        systemArmed = true;
        publishEvent("SYSTEM_ARMED");

        Serial.println("Security system ARMED");
      }
    }

    else if (message.equalsIgnoreCase("DISARM")) {

      if (systemArmed) {

        systemArmed = false;
        publishEvent("SYSTEM_DISARMED");

        Serial.println("Security system DISARMED");
      }
    }
  }
}

// =====================================================
// 8. WIFI CONNECTION
// =====================================================

void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWiFiAttempt < WIFI_RETRY_INTERVAL) {
    return;
  }

  lastWiFiAttempt = millis();

  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// =====================================================
// 9. MQTT CONNECTION
// =====================================================

void reconnectMQTT() {

  if (WiFi.status() != WL_CONNECTED) return;

  if (mqtt.connected()) return;

  if (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL) {
    return;
  }

  lastMQTTAttempt = millis();

  Serial.print("Connecting to MQTT...");

  String clientId = "SmartGuard_ESP32_";
  clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqtt.connect(clientId.c_str(),
                   statusTopic.c_str(),
                   0,
                   true,
                   "OFFLINE")) {

    Serial.println("Connected!");

    mqtt.subscribe(ledCommandTopic.c_str());
    mqtt.subscribe(armedCommandTopic.c_str());

    mqtt.publish(statusTopic.c_str(), "ONLINE", true);

    Serial.println("MQTT subscriptions successful.");
  }

  else {

    Serial.print("Connection failed. State: ");
    Serial.println(mqtt.state());
  }
}

// =====================================================
// 10. SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(IR_PIN, INPUT);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(MQ_PIN, ADC_11db);

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.println("=================================");
  Serial.println("       SMARTGUARD SYSTEM");
  Serial.println("=================================");

  Serial.println("Initializing sensors...");
  Serial.println("Connecting to WiFi...");
}

// =====================================================
// 11. MAIN LOOP
// =====================================================

void loop() {

  // Maintain WiFi connection
  connectWiFi();

  // Maintain MQTT connection
  reconnectMQTT();

  // Process incoming MQTT messages
  if (mqtt.connected()) {
    mqtt.loop();
  }

  // ---------------------------------------------------
  // READ SENSORS
  // ---------------------------------------------------

  int smokeRaw = analogRead(MQ_PIN);

  int pirValue = digitalRead(PIR_PIN);
  int irValue = digitalRead(IR_PIN);

  bool motionDetected = (pirValue == HIGH);

  bool obstacleDetected =
      (irValue == IR_DETECTED_LEVEL);

  bool gasAlert = (smokeRaw >= MQ_THRESHOLD);

  // ---------------------------------------------------
  // GAS ALERT EVENTS
  // ---------------------------------------------------

  if (gasAlert && !previousGasAlert) {
    publishEvent("GAS_WARNING");
  }

  if (!gasAlert && previousGasAlert) {
    publishEvent("GAS_NORMAL");
  }

  previousGasAlert = gasAlert;

  // ---------------------------------------------------
  // MOTION ALERT EVENTS
  // ---------------------------------------------------

  if (systemArmed && motionDetected && !previousMotion) {
    publishEvent("MOTION_DETECTED");
  }

  previousMotion = motionDetected;

  // ---------------------------------------------------
  // OBSTACLE ALERT EVENTS
  // ---------------------------------------------------

  if (obstacleDetected && !previousObstacle) {
    publishEvent("OBSTACLE_DETECTED");
  }

  previousObstacle = obstacleDetected;

  // ---------------------------------------------------
  // UPDATE LEDs
  // ---------------------------------------------------

  updateLEDs(gasAlert,
             motionDetected,
             obstacleDetected);

  // ---------------------------------------------------
  // SERIAL MONITOR
  // ---------------------------------------------------

  if (millis() - lastSerialPrint >= SERIAL_INTERVAL) {

    lastSerialPrint = millis();

    Serial.print("Gas Raw: ");
    Serial.print(smokeRaw);

    Serial.print(" | Motion: ");
    Serial.print(motionDetected ? "DETECTED" : "CLEAR");

    Serial.print(" | Obstacle: ");
    Serial.print(obstacleDetected ? "DETECTED" : "CLEAR");

    Serial.print(" | Security: ");
    Serial.println(systemArmed ? "ARMED" : "DISARMED");
  }

  // ---------------------------------------------------
  // MQTT TELEMETRY
  // ---------------------------------------------------

  if (millis() - lastTelemetry >= TELEMETRY_INTERVAL) {

    lastTelemetry = millis();

    if (mqtt.connected()) {

      String json = "{";

      json += "\"smoke_raw\":" + String(smokeRaw) + ",";
      json += "\"motion\":" + String(motionDetected ? 1 : 0) + ",";
      json += "\"obstacle\":" + String(obstacleDetected ? 1 : 0) + ",";
      json += "\"armed\":" + String(systemArmed ? 1 : 0) + ",";
      json += "\"gas_alert\":" + String(gasAlert ? 1 : 0);

      json += "}";

      mqtt.publish(telemetryTopic.c_str(), json.c_str());

      Serial.println("Telemetry published:");
      Serial.println(json);
    }
  }

  delay(20);
}
