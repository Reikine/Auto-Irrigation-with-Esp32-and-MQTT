#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// --- KONFIGURASI WIFI & MQTT ---
const char *ssid = "POCO M3 Pro 5G";
const char *password = "11111111";
const char *mqtt_server = "broker.emqx.io";

const char *topic_control = "riku/garden/control";
const char *topic_data = "riku/garden/full_data";

// --- KONFIGURASI OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI 23
#define OLED_CLK 18
#define OLED_DC 2
#define OLED_CS 5
#define OLED_RESET 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

const int PIN_SENSOR_1 = 34;
const int PIN_SENSOR_2 = 35;
const int PIN_RELAY_1 = 26;
const int PIN_RELAY_2 = 27;
const int PIN_BUZZER = 25;

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// --- VARIABEL GLOBAL ---
unsigned long lastMsg = 0;
bool manualOverride = false;
bool manualP1 = false;
bool manualP2 = false;
int batasKering = 30;

void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.print("Pesan masuk: ");
  Serial.println(message);

  if (isdigit(message[0]))
  {
    int nilaiBaru = message.toInt();
    if (nilaiBaru > 0 && nilaiBaru < 100)
    {
      batasKering = nilaiBaru;

      preferences.begin("garden-data", false);
      preferences.putInt("limit", batasKering);
      preferences.end();

      Serial.print("Setting tersimpan baru: ");
      Serial.println(batasKering);
    }
  }
  else if (message == "Pompa 1 Hidup")
  {
    digitalWrite(PIN_RELAY_1, HIGH);
    manualOverride = true;
    manualP1 = true;
  }
  else if (message == "Pompa 1 Mati")
  {
    digitalWrite(PIN_RELAY_1, LOW);
    manualOverride = true;
    manualP1 = false;
  }
  else if (message == "Pompa 2 Hidup")
  {
    digitalWrite(PIN_RELAY_2, HIGH);
    manualOverride = true;
    manualP2 = true;
  }
  else if (message == "Pompa 2 Mati")
  {
    digitalWrite(PIN_RELAY_2, LOW);
    manualOverride = true;
    manualP2 = false;
  }
  else if (message == "Otomatis")
  {
    manualOverride = false;
    Serial.println("Kembali ke Mode Otomatis");
  }
}

void setup_wifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
}

void reconnect()
{
  while (!client.connected())
  {
    String clientId = "Riku-ESP32-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str()))
    {
      client.publish("riku/garden/status", "System Online");
      client.subscribe(topic_control);
    }
    else
    {
      delay(5000);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  // Setup Pin
  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SENSOR_1, INPUT);
  pinMode(PIN_SENSOR_2, INPUT);

  preferences.begin("garden-data", true);
  batasKering = preferences.getInt("limit", 30);
  preferences.end();

  // Setup Layar
  if (!display.begin(SSD1306_SWITCHCAPVCC))
  {
    Serial.println(F("Gagal memulai...."));
    for (;;)
      ;
  }

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop()
{
  if (!client.connected())
    reconnect();
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 2000)
  { // Kirim data tiap 2 detik
    lastMsg = now;

    // Baca Sensor
    int raw1 = analogRead(PIN_SENSOR_1);
    int raw2 = analogRead(PIN_SENSOR_2);
    int moist1 = map(raw1, 0, 4095, 0, 100);
    int moist2 = map(raw2, 0, 4095, 0, 100);

    // Logika Kontrol
    String statusP1 = "OFF";
    String statusP2 = "OFF";

    if (manualOverride)
    {
      statusP1 = manualP1 ? "MAN-ON" : "MAN-OFF";
      statusP2 = manualP2 ? "MAN-ON" : "MAN-OFF";
    }
    else
    {

      if (moist1 < batasKering)
      {
        digitalWrite(PIN_RELAY_1, HIGH);
        statusP1 = "AUTO-ON";
        tone(PIN_BUZZER, 1000, 100);
      }
      else
      {
        digitalWrite(PIN_RELAY_1, LOW);
      }

      if (moist2 < batasKering)
      {
        digitalWrite(PIN_RELAY_2, HIGH);
        statusP2 = "AUTO-ON";
        tone(PIN_BUZZER, 1000, 100);
      }
      else
      {
        digitalWrite(PIN_RELAY_2, LOW);
      }
    }

    // Tampilan OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    if (manualOverride)
      display.println("-- MANUAL MODE --");
    else
    {
      display.print("AUTO (Limit:");
      display.print(batasKering);
      display.println("%)");
    }

    display.setCursor(0, 15);
    display.print("Tanaman 1: ");
    display.print(moist1);
    display.print("% ");
    display.println(statusP1);
    display.setCursor(0, 30);
    display.print("Tanaman 2: ");
    display.print(moist2);
    display.print("% ");
    display.println(statusP2);
    display.display();

    // Kirim Data ke Web (JSON)
    String p1State = digitalRead(PIN_RELAY_1) ? "ON" : "OFF";
    String p2State = digitalRead(PIN_RELAY_2) ? "ON" : "OFF";

    // Format: {"s1":40,"s2":60,"p1":"OFF","p2":"ON","lim":35}
    String json = "{\"s1\":" + String(moist1) +
                  ",\"s2\":" + String(moist2) +
                  ",\"p1\":\"" + p1State + "\"" +
                  ",\"p2\":\"" + p2State + "\"" +
                  ",\"lim\":" + String(batasKering) + "}";

    client.publish(topic_data, json.c_str());
  }
}