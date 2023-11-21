#include "Adafruit_LC709203F.h"
#include "esp32-hal-adc.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Bounce2.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>

#include <elapsedMillis.h>
#ifndef CONFIG_LITTLEFS_FOR_IDF_3_2
#include <time.h>
#endif

#include <Syslog.h>

// Wifi Credentials
const char *ssid = "vault24";
const char *password = "Atomix1040";

// NTP Sever initalization
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C
#define BUTTON_PIN 18
#define TICK_TIME 1500
#define SEND_VALUES_TIME 50000
#define MONITOR_LED_PIN0 5
#define MONITOR_LED_PIN1 17

#define FORMAT_LITTLEFS_IF_FAILED true

Adafruit_LC709203F lc;
Adafruit_SSD1306 display;
Bounce button;
elapsedMillis display_update_time, send_values_time;
Bounce led0, led1;

class PowerTick0
{
  public:
    PowerTick0()
    {
        getLocalTime(&timeInfo);
    }
    uint8_t index = 0;
    struct tm timeInfo;
};

class PowerTick1
{
  public:
    PowerTick1()
    {
        getLocalTime(&timeInfo);
    }
    uint8_t index = 1;
    struct tm timeInfo;
};

std::vector<PowerTick0> ticks0;
std::vector<PowerTick1> ticks1;

struct status_t
{
    bool display = false;
    bool fuelgauge = false;
    bool bt = false;
    bool wifi = false;
} status;

void display_status(void)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    if (status.fuelgauge)
    {
        display.print("V:");
        display.println(lc.cellVoltage());
        display.print("%:");
        display.println(lc.cellPercent());
        display.print("MHz: ");
        display.println(getCpuFrequencyMhz());
    }
    else
    {
        display.print("NO BATT STAT");
    }
    display.display();
}

String localTimeAsString()
{
    time_t rawtime;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        char timeHour[128];
        strftime(timeHour, 128, "%A, %B %d %Y %H:%M:%S", &timeinfo);
        return (String(timeHour));
    }
    else
    {
        return (String(""));
    }
}

unsigned long iteration = 1;
#define SYSLOG_SERVER "192.168.0.111"
#define SYSLOG_PORT 514

// This device info
#define DEVICE_HOSTNAME "onedaypower"
#define APP_NAME "main"

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("mqttClient topic: ");
    Serial.println(topic);
}

WiFiUDP udpClient;
Syslog syslog(udpClient, SYSLOG_SERVER, SYSLOG_PORT, DEVICE_HOSTNAME, APP_NAME, LOG_KERN);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool sync_time()
{

    if (WiFi.isConnected())
    {
        Serial.print(" CONNECTED: ip: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println(" NOT CONNECTED can't sync");
        return false;
    }
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Failed to obtain time");
        return false;
    }
    else
    {
        return true;
    }
}

void enable_wifi_and_sync()
{
    setCpuFrequencyMhz(240);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    WiFi.setHostname("onedaypower");

    for (int i = 0; i < 10; i++)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            delay(250);
            Serial.print(".");
        }
    }

    if (!MDNS.begin("onedaypower"))
    {
        Serial.println("Error starting mDNS");
    }
    delay(150);

    delay(50);
    const bool sync = sync_time();

    syslog.logf(LOG_INFO, "WIFI IP:%s time_sync:%d ", WiFi.localIP().toString(), sync, iteration);
    iteration++;

    mqttClient.setServer("192.168.0.111", 1883);
    mqttClient.setCallback(mqttCallback);
    String ticksString;
    for (PowerTick0 tick : ticks0)
    {
        char buf[256];
        strftime(buf, 256, "%H:%M:%S", dynamic_cast<const tm *>(&tick.timeInfo));
    }
    for (auto tick : ticks1)
    {
        char buf[256];
        strftime(buf, 256, "%H:%M:%S", dynamic_cast<const tm *>(&tick.timeInfo));
    }
}

void disable_wifi_and_slow_down()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    delay(50);
    Serial.println("shutdown bt");
    btStop();
    esp_bt_controller_disable();

    delay(50);
    Serial.print("slow down the cpu to 80MHz ");
    if (setCpuFrequencyMhz(80))
    {
        Serial.println("success");
    }
    else
    {
        Serial.println("failed");
    }
}

void setup()
{
    Wire.begin(23, 19);

    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    button.attach(BUTTON_PIN);
    display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
    Serial.println("init display:");
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println(F("SSD1306 allocation failed"));
        status.display = false;
    }
    else
    {
        status.display = true;
        display.clearDisplay();
        display.display();
    }

    if (!lc.begin(&Wire))
    {
        Serial.println(F("Couldnt find Adafruit LC709203F? Make sure a battery is "
                         "plugged in!"));
        status.fuelgauge = false;
    }
    else
    {
        status.fuelgauge = true;
        Serial.println(F("Found LC709203F"));
        lc.setPackSize(LC709203F_APA_3000MAH);
    }

    delay(50);
    Serial.println("attach bound to led monitoring pins");
    pinMode(MONITOR_LED_PIN0, INPUT);
    pinMode(MONITOR_LED_PIN1, INPUT);
    led0.attach(MONITOR_LED_PIN0);
    led1.attach(MONITOR_LED_PIN1);

    delay(150);
    Serial.printf("WIFI Connecting to %s ", ssid);
    enable_wifi_and_sync();

    Serial.println("shutdown wifi");
    delay(50);

    delay(50);
    Serial.println("setup end");
    Serial.println();
}

void loop()
{
    led0.update();
    led1.update();
    button.update();
    if (led0.changed())
    {
        Serial.print("led0 changed ");
        Serial.println(led0.read());
        ticks0.push_back(PowerTick0());
    }
    if (led1.changed())
    {
        Serial.println("led1 changed ");
        Serial.println(led1.read());
        ticks1.push_back(PowerTick1());
    }

    if (button.read() == false)
    {
        if (display_update_time >= 150)
        {
            display_update_time = display_update_time - 150;

            Serial.println("printing status to oled");
            display_status();
        }
    }
    else
    {
        if (display_update_time >= TICK_TIME)
        {
            display_update_time = display_update_time - TICK_TIME;
            Serial.println("tick!");
            display.clearDisplay();
            display.display();
        }
    }

    if (send_values_time >= SEND_VALUES_TIME)
    {
        send_values_time = send_values_time - SEND_VALUES_TIME;
        Serial.println("time to tunr wifi on and send value");
        enable_wifi_and_sync();
        syslog.logf(LOG_INFO, "boot IP:%s  led0:%d led1:%d, voltage:%2.2f, percent:%2.2f", WiFi.localIP().toString(),
                    ticks0.size(), ticks1.size(), lc.cellVoltage(), lc.cellPercent(), iteration++);
    }
}