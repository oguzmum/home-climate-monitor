#include <WiFiManager.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include "../secrets.h"

#define DHT_PIN 4
#define DHT_TYPE DHT11

#define uS_TO_S_FACTOR 1000000ULL
const uint64_t DEEP_SLEEP_INTERVAL_S = 60;

char roomName[32] = "unknown";
String climateTopic;
String statusTopic;

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void goToSleep() {
    mqttClient.disconnect();
    WiFi.disconnect(true);
    Serial.println("Going to deep sleep...");
    Serial.flush();
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_INTERVAL_S * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

void publishStatus(const char* status) {
    StaticJsonDocument<128> doc;
    doc["room"] = roomName;
    doc["status"] = status;

    char payload[128];
    size_t payloadLength = serializeJson(doc, payload);
    mqttClient.publish(statusTopic.c_str(), payload, payloadLength);
}

bool connectToMqtt() {
    const int MAX_ATTEMPTS = 5;
    int attempts = 0;

    while (!mqttClient.connected() && attempts < MAX_ATTEMPTS) {
        attempts++;
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
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" -> retrying in 5s");
            delay(4000);
        }
    }

    return mqttClient.connected();
}

void connectToWifi() {
    Preferences preferences;
    preferences.begin("config", true);
    String savedRoomName = preferences.getString("room", "");
    preferences.end();

    WiFiManager wifiManager;
    WiFiManagerParameter roomNameParam("room", "Room Name", savedRoomName.c_str(), 32);
    wifiManager.addParameter(&roomNameParam);
    wifiManager.setConfigPortalTimeout(180); // don't sit in AP mode forever on battery

    bool connected;
    if (savedRoomName.length() == 0) {
        connected = wifiManager.startConfigPortal("ESP32-Setup");
    } else {
        connected = wifiManager.autoConnect("ESP32-Setup");
    }

    if (!connected) {
        Serial.println("WiFi connection failed, restarting...");
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
    }
}

void setup() {
    Serial.begin(115200);
    dht.begin();

    connectToWifi();

    climateTopic = "home/sensors/" + String(roomName) + "/climate";
    statusTopic = "home/sensors/" + String(roomName) + "/status";

    Serial.print("Room: ");
    Serial.println(roomName);
    Serial.print("Topic: ");
    Serial.println(climateTopic);

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

    if (connectToMqtt()) {
        float humidity = dht.readHumidity();
        float temperature = dht.readTemperature();

        if (!isnan(temperature) && !isnan(humidity)) {
            publishClimateData(temperature, humidity);
        } else {
            Serial.println("Failed to read from DHT11 sensor!");
            publishStatus("sensor_error");
        }
    } else {
        Serial.println("Could not reach MQTT broker, skipping this cycle.");
		publishStatus("could_not_connect_to_mqtt");
    }

    goToSleep();
}

void loop() {
    // never reached: deep sleep resets the chip, which re-runs setup()
}
