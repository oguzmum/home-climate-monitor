#include <WiFiManager.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include "../secrets.h"

#define DHT_PIN 4
#define DHT_TYPE DHT11
#define LED_PIN 2

const unsigned long PUBLISH_INTERVAL_MS = 60000; //60s

char roomName[32] = "unknown";
String climateTopic;
String statusTopic;

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastPublishTime = 0;

void blinkLed(int times, int onDurationMs, int offDurationMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(onDurationMs);
        digitalWrite(LED_PIN, LOW);
        if (i < times - 1) delay(offDurationMs);
    }
}

void publishStatus(const char* status) {
    StaticJsonDocument<128> doc;
    doc["room"] = roomName;
    doc["status"] = status;

    char payload[128];
    size_t payloadLength = serializeJson(doc, payload);
    mqttClient.publish(statusTopic.c_str(), payload, payloadLength);
}

void connectToMqtt() {
    while (!mqttClient.connected()) {
        Serial.print("Connecting to MQTT broker...");
        String clientId = "esp32-" + String(roomName) + "-" + String(random(0xffff), HEX);

        bool connected;
        if (strlen(MQTT_USER) > 0) {
            connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
        } else {
            connected = mqttClient.connect(clientId.c_str());
        }

        if (connected) {
            Serial.println("connected!");
            publishStatus("online");
            digitalWrite(LED_PIN, HIGH);
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" -> retrying in 5s");
            blinkLed(2, 500, 500); // 2 slow blinks = no MQTT
            delay(4000);
        }
    }
}

void connectToWifi() {
    Preferences preferences;
    preferences.begin("config", true);
    String savedRoomName = preferences.getString("room", "");
    preferences.end();

    WiFiManager wifiManager;
    WiFiManagerParameter roomNameParam("room", "Room Name", savedRoomName.c_str(), 32);
    wifiManager.addParameter(&roomNameParam);

    bool connected;
    if (savedRoomName.length() == 0) {
        connected = wifiManager.startConfigPortal("ESP32-Setup");
    } else {
        connected = wifiManager.autoConnect("ESP32-Setup");
    }

    if (!connected) {
        Serial.println("WiFi connection failed, restarting...");
        blinkLed(5, 100, 100); // 5 fast blinks = no WiFi
        delay(500);
        ESP.restart();
    }

    String enteredRoomName = String(roomNameParam.getValue());
    if (enteredRoomName.length() > 0 && enteredRoomName != savedRoomName) {
        preferences.begin("config", false);
        preferences.putString("room", enteredRoomName);
        preferences.end();
        savedRoomName = enteredRoomName;
    }

    strlcpy(roomName, savedRoomName.length() > 0 ? savedRoomName.c_str() : "unknown", sizeof(roomName));
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    dht.begin();

    connectToWifi();

    climateTopic = "home/sensors/" + String(roomName) + "/climate";
    statusTopic = "home/sensors/" + String(roomName) + "/status";

    Serial.print("Room: ");
    Serial.println(roomName);
    Serial.print("Topic: ");
    Serial.println(climateTopic);

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    connectToMqtt();
}

void publishClimateData(float temperature, float humidity) {
    StaticJsonDocument<128> doc;
    doc["room"] = roomName;
    doc["temp"] = round(temperature * 10) / 10.0;
    doc["humidity"] = round(humidity * 10) / 10.0;

    char payload[128];
    size_t payloadLength = serializeJson(doc, payload);

    bool success = mqttClient.publish(climateTopic.c_str(), payload, payloadLength);
    Serial.print("Published to ");
    Serial.print(climateTopic);
    Serial.print(": ");
    Serial.print(payload);
    Serial.println(success ? "  [OK]" : "  [FAILED]");

    if (!success) {
        publishStatus("publish_error");
        blinkLed(3, 200, 200); // 3 medium blinks = publish failed
        digitalWrite(LED_PIN, HIGH);
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, restarting...");
        blinkLed(5, 100, 100); // 5 fast blinks = no WiFi
        delay(500);
        ESP.restart();
    }

    if (!mqttClient.connected()) {
        connectToMqtt();
    }
    mqttClient.loop();

    unsigned long currentTime = millis();
    if (currentTime - lastPublishTime >= PUBLISH_INTERVAL_MS) {
        lastPublishTime = currentTime;

        float humidity = dht.readHumidity();
        float temperature = dht.readTemperature();

        if (!isnan(temperature) && !isnan(humidity)) {
            publishClimateData(temperature, humidity);
        } else {
            Serial.println("Failed to read from DHT11 sensor!");
            publishStatus("sensor_error");
            blinkLed(3, 100, 100); // 3 fast blinks = sensor error
            digitalWrite(LED_PIN, HIGH);
        }
    }
}
