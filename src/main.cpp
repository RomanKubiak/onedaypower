#include "Adafruit_LC709203F.h"
#include "esp32-hal-adc.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Bounce2.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Wire.h>
#include <elapsedMillis.h>

#ifndef CONFIG_LITTLEFS_FOR_IDF_3_2
#include <time.h>
#endif

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C
#define BUTTON_PIN 22
#define TICK_TIME 1500
#define FORMAT_LITTLEFS_IF_FAILED true

Adafruit_LC709203F lc;
Adafruit_SSD1306 display;
Bounce button;
elapsedMillis display_update_time;

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
        Serial.print("Version: 0x");
        Serial.println(lc.getICversion(), HEX);
        lc.setThermistorB(3950);
        Serial.print("Thermistor B = ");
        Serial.println(lc.getThermistorB());
        lc.setPackSize(LC709203F_APA_3000MAH);
    }

    Serial.println("shutdown wifi");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("shutdown bt");
    btStop();
    esp_bt_controller_disable();
    Serial.print("slow down the cpu to 80MHz ");
    if (setCpuFrequencyMhz(80))
    {
        Serial.println("success");
    }
    else
    {
        Serial.println("failed");
    }

    Serial.print("slow down the cpu to 40MHz ");
    if (setCpuFrequencyMhz(40))
    {
        Serial.println("success");
    }
    else
    {
        Serial.println("failed");
    }
}

void loop()
{
    button.update();
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
}