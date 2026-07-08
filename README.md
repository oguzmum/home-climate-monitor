# Home Climate Monitor

My idea for this project: distributed sensor nodes → MQTT → time-series DB → Grafana. Plus a dedicated e-ink display that shows live room climate around the clock.

The broker and backend stack (Mosquitto, Telegraf, InfluxDB, Grafana) run on my self-hosted home server. Each ESP32 sensor node connects over WiFi and publishes data directly to it.

## Architecture

```mermaid
flowchart LR
    A["ESP32 + DHT11<br/>(per room)"] -->|"MQTT publish<br/>home/sensors/&lt;room&gt;/climate"| B

    subgraph server["Home Server"]
        B["Mosquitto<br/>(Broker)"]
        B --> C["Telegraf<br/>(Bridge)"]
        C --> D["InfluxDB<br/>(Time-series DB)"]
        D --> E["Grafana<br/>(Web Dashboard)"]
    end

    E --> F["ESP32 + E-Ink<br/>(24/7 display)"]
```

## Data Flow

1. Each sensor node reads temperature and humidity frequently and publishes a JSON payload:
   ```json
   { "room": "livingroom", "temp": 21.5, "humidity": 45.2 }
   ```
   to `home/sensors/livingroom/climate`.
2. **Mosquitto** receives the message and makes it available to any subscriber.
3. **Telegraf** subscribes to `home/sensors/+/climate` (wildcard across all rooms), parses the JSON, and writes each measurement into **InfluxDB** with `room` as a tag.
4. **Grafana** queries InfluxDB and renders time-series panels, with a dashboard variable to filter by room.
5. The **e-ink display** node renders a Grafana snapshot image or queries InfluxDB/MQTT directly for a lightweight custom view.

## Sensor Node Setup

Built with [PlatformIO](https://platformio.org). Open the `sensor/` folder in VS Code with the PlatformIO extension installed.

### 1. Configure secrets

Copy `sensor/secrets.h.example` to `sensor/secrets.h` and fill in your MQTT broker details:

```cpp
#define MQTT_BROKER "x.x.x.x"
#define MQTT_PORT   1883
#define MQTT_USER   ""
#define MQTT_PASS   ""
```

WiFi credentials are **not** set in code - they are configured per device via the captive portal (see below).

### 2. Flash the firmware

```bash
pio run --target upload
```

The same firmware binary works for every room - no code changes needed per device.

### 3. First-time device setup (WiFiManager)

On first boot the ESP32 opens a setup access point called **`ESP32-Setup`**:

1. Connect your phone or laptop to the `ESP32-Setup` WiFi network
2. A captive portal opens automatically (or navigate to `192.168.4.1`)
3. Enter your WiFi network name, password, and the room name (e.g. `bedroom`)
4. Hit save - the ESP32 connects and starts publishing

From the next boot onwards it connects automatically without showing the portal. WiFi credentials and room name are stored persistently on the device.

To reconfigure a device (new WiFi or room name), erase the flash first:

```bash
pio run --target erase
```

Then re-flash and go through the portal again.

### Status monitoring

Each node also publishes to `home/sensors/<room>/status`. Subscribe to all rooms at once. Or use [MQTT Explorer](https://mqtt-explorer.com)

```bash
mosquitto_sub -h <broker-ip> -t "home/sensors/+/status"
```

## Infrastructure

The Mosquitto broker, Telegraf, and InfluxDB run via Docker Compose:

```bash
cd infrastructure/
docker compose up -d
```

---

## Gallery

| Hardware | Dashboard |
|---|---|
| ![ESP32 + DHT11 on breadboard](./images/breadboard-esp32-dht11.PNG) | ![Grafana dashboard](./images/grafana-dashboard.png) |
| ESP32 sensor node wired up with DHT11 | Grafana showing temperature & humidity over time (A json of the dashboard can be found [here](./infrastructure/grafana-dashboard.json))|

---

## Power consumption & deep sleep

First I'll use **9V batteries**, which has a small capacity (~500 mAh) and can't sustain a WiFi-connected ESP32 running continuously for very long. To stretch battery life I used the ESP32's **deep sleep** instead of an always-on loop.

**Roughly how long the battery will last:**

This is a rough estimate, not a lab measurement - actual runtime depends a lot on the specific battery and board.

Assumptions:

| Parameter | Value |
|---|---|
| Wake interval `T` |60 s |
| Active time per cycle `t_active` (WiFi + MQTT + DHT read) | ~3 s |
| Active current `I_active` (WiFi radio + CPU) | ~150 mA |
| Deep sleep current `I_sleep` | 0.01 mA (bare ESP32) up to ~8 mA (typical DevKit board, onboard LDO + USB-UART chip keep drawing current even in deep sleep) |
| 9V battery capacity | ~500 mAh (standard alkaline) |

Average current draw:

```
I_avg = (t_active × I_active + t_sleep × I_sleep) / T
```

- **Best case** (bare ESP32 module, minimal sleep draw):
  `I_avg = (3s × 150mA + (60s - 3s) × 0.01mA) / 60s ≈ 7.5 mA`
  → `500 mAh / 7.5 mA ≈ 66.6 h` (**~2.7 days**)

- **Realistic case** (stock `esp32dev` board - the onboard voltage regulator and USB-serial chip keep drawing power in deep sleep too):
  `I_avg = (3s × 150mA + 57s × 8mA) / 60s ≈ 15.1 mA`
  → `500 mAh / 15.1 mA ≈ 33 h` (**~1.4 days**)

For comparison, without deep sleep the node draws ~150 mA continuously, which would drain the same battery in about `500 mAh / 150 mA ≈ 3.3 h`.

For the long run I'll switch to a larger battery pack :D