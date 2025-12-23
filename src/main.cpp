#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char *ssid = "Wokwi-GUEST";
const char *password = "";
const char *mqtt_server = "broker.emqx.io";

const char *topic_control = "riku/garden/control";
const char *topic_data = "riku/garden/full_data";
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
const int PIN_SENSOR_1 = 34;
const int PIN_SENSOR_2 = 35;
const int PIN_RELAY_1 = 26;
const int PIN_RELAY_2 = 27;
const int PIN_BUZZER = 4;
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
bool manualOverride = false;
bool manualP1 = false;
bool manualP2 = false;

void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.print("Pesan masuk [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  if (message == "Pompa 1 Hidup")
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
    Serial.println("Sistem kembali ke mode Otomatis");
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
    String clientId = "Riku-Dual-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str()))
    {
      client.publish("riku/garden/status", "System Online");

      client.subscribe(topic_control);
      Serial.println("Mendengarkan perintah...");
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
  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SENSOR_1, INPUT);
  pinMode(PIN_SENSOR_2, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    for (;;)
      ;
  }
  display.clearDisplay();

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // Daftarkan fungsi callback
}

void loop()
{
  if (!client.connected())
    reconnect();
  client.loop(); // Wajib ada agar bisa terima pesan

  unsigned long now = millis();
  if (now - lastMsg > 2000)
  {
    lastMsg = now;

    int raw1 = analogRead(PIN_SENSOR_1);
    int raw2 = analogRead(PIN_SENSOR_2);
    int moist1 = map(raw1, 0, 4095, 0, 100);
    int moist2 = map(raw2, 0, 4095, 0, 100);

    String statusP1 = "OFF";
    String statusP2 = "OFF";

    if (manualOverride)
    {

      statusP1 = manualP1 ? "MAN-ON" : "MAN-OFF";
      statusP2 = manualP2 ? "MAN-ON" : "MAN-OFF";
    }
    else
    {

      if (moist1 < 30)
      {
        digitalWrite(PIN_RELAY_1, HIGH);
        statusP1 = "AUTO-ON";
        tone(PIN_BUZZER, 1000, 100);
      }
      else
      {
        digitalWrite(PIN_RELAY_1, LOW);
      }

      if (moist2 < 30)
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

    // Update OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(manualOverride ? "--- MANUAL MODE ---" : "--- AUTO MODE ---");

    display.setCursor(0, 15);
    display.print("Plant A: ");
    display.print(moist1);
    display.print("% ");
    display.println(statusP1);
    display.setCursor(0, 30);
    display.print("Plant B: ");
    display.print(moist2);
    display.print("% ");
    display.println(statusP2);
    display.display();

    // Kirim Data
    String p1State = digitalRead(PIN_RELAY_1) ? "ON" : "OFF";
    String p2State = digitalRead(PIN_RELAY_2) ? "ON" : "OFF";

    String json = "{\"s1\":" + String(moist1) +
                  ",\"s2\":" + String(moist2) +
                  ",\"p1\":\"" + p1State + "\"" +
                  ",\"p2\":\"" + p2State + "\"}";

    // Hasilnya nanti: {"s1":40,"s2":60,"p1":"ON","p2":"OFF"}
    client.publish(topic_data, json.c_str());
  }
}