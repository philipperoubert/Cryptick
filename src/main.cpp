// clang-format off
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "epd_driver.h"
#include <Wire.h>
#include "crypto.h"
#include "utils.h"
#include "line_plotting.h"
#include "roboto.h"
#include "cryptick.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
// clang-format on

uint8_t *framebuffer = NULL;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
#define BUTTON_3 21
#define BATT_PIN 14
int vref = 1100;
void setup()
{
  pinMode(BUTTON_3, INPUT_PULLUP);
  Serial.begin(115200);

  unsigned long buttonPressStartTime = 0;
  bool buttonPressed = false;

  while (true)
  {
    int buttonState = digitalRead(BUTTON_3);

    if (buttonState == LOW)
    { // Assuming the button is active LOW
      if (!buttonPressed)
      {
        buttonPressed = true;
        buttonPressStartTime = millis();
      }
      else
      {
        if (millis() - buttonPressStartTime >= 3000)
        {
          // Button has been pressed for 3 seconds, clear saved WiFi credentials
          clearSavedWiFiCredentials();
          epd_init();
          epd_poweron();
          epd_clear();
          epd_draw_grayscale_image(epd_full_screen(), (uint8_t *)cryptick_data);
          epd_poweroff();
          esp_deep_sleep_start();
        }
      }
    }
    else
    {
      buttonPressed = false;
      break;
    }
  }

  bool connected = tryConnectSavedNetwork();
  Serial.println(connected);
  if (connected)
  {
    Serial.println("Connected to saved network");
    Serial.println("WiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer)
    {
      Serial.println("alloc memory failed !!!");
      while (1)
        ;
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    Serial.println("Drawing header...");
    FontProperties props = {.fg_color = 0, .bg_color = 15, .fallback_glyph = 0, .flags = 0};

    int cursor_x = 30;
    int cursor_y = 65;
    Serial.println("Drawing header...");
    write_mode((GFXfont *)&roboto, "Symbol", &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
    cursor_x = 130;
    write_mode((GFXfont *)&roboto, "Price", &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
    cursor_x = 280;
    write_mode((GFXfont *)&roboto, "1h", &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
    cursor_x = 380;
    write_mode((GFXfont *)&roboto, "24h", &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
    cursor_x = 480;
    write_mode((GFXfont *)&roboto, "7d", &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
    cursor_x = 640;
    write_mode((GFXfont *)&roboto, "24h Chart", &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);

    bresenham(30, 93, 30 + 890, 93, 0, false, framebuffer);
    bresenham(30, 94, 30 + 890, 94, 0, false, framebuffer);

    String crypto_names[5];

    preferences.begin("CryptoCreds", true);
    for (int i = 0; i < 5; i++)
    {
      String key = "crypto" + String(i + 1);
      crypto_names[i] = preferences.getString(key.c_str(), "");
    }
    preferences.end();

    String cryptolist = "";
    for (int i = 0; i < 5; i++)
    {
      if (crypto_names[i] == "")
      {
        continue;
      }
      cryptolist += crypto_names[i];
      if (i < 4)
      {
        cryptolist += ",";
      }
    }

    Serial.println("Fetching cryptocurrency change data...");
    DynamicJsonDocument change = getChange(cryptolist);
    Serial.println("Received cryptocurrency change data.");

    for (int i = 0; i < 5; i++)
    {
      if (crypto_names[i] == "")
      {
        continue;
      }
      Serial.printf("Processing data for %s...\n", crypto_names[i].c_str());
      JsonArrayConst prices = getHistory(crypto_names[i]);

      double min_price = 0;
      double max_price = 0;

      for (int j = 0; j < prices.size(); j++)
      {
        if (prices[j][1] < min_price || min_price == 0)
        {
          min_price = prices[j][1];
        }
        if (prices[j][1] > max_price || max_price == 0)
        {
          max_price = prices[j][1];
        }
      }

      double last_price = prices[0][1];

      int x = 640;
      int width = 280;
      int y = 110 + i * 80;
      int height = 60;

      Serial.println("Drawing price chart...");
      for (int j = 0; j < prices.size() - 1; j++)
      {
        int x0 = (int)(x + j * width / prices.size() - 1);
        int x1 = (int)(x + (j + 1) * width / prices.size() - 1);

        double current_price = prices[j + 1][1];

        if (current_price == 0)
        {
          current_price = last_price;
        }
        int y0 = (int)(y + (max_price - last_price) / (max_price - min_price) * height);
        int y1 = (int)(y + (max_price - current_price) / (max_price - min_price) * height);
        last_price = current_price;
        bresenham(x0, y0, x1, y1, y + height, true, framebuffer);
        bresenham(x0, y0 + 1, x1, y1 + 1, y + height, false, framebuffer);
      }

      Serial.println("Writing cryptocurrency data...");
      String symbol;
      const char *symbol_char = change[i]["symbol"];
      symbol = (String)symbol_char;
      symbol.toUpperCase();
      double price = change[i]["current_price"];
      double hour_change = change[i]["price_change_percentage_1h_in_currency"];
      double day_change = change[i]["price_change_percentage_24h_in_currency"];
      double week_change = change[i]["price_change_percentage_7d_in_currency"];

      String str_price = (String)price;
      String str_hour_change = (String)hour_change + "%";
      String str_day_change = (String)day_change + "%";
      String str_week_change = (String)week_change + "%";

      cursor_x = 30;
      cursor_y = 148 + i * 80;
      write_mode((GFXfont *)&roboto, &symbol[0], &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
      cursor_x = 130;
      write_mode((GFXfont *)&roboto, &str_price[0], &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
      cursor_x = 280;
      write_mode((GFXfont *)&roboto, &str_hour_change[0], &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
      cursor_x = 380;
      write_mode((GFXfont *)&roboto, &str_day_change[0], &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
      cursor_x = 480;
      write_mode((GFXfont *)&roboto, &str_week_change[0], &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);
    }
    Serial.println("Configuring time...");
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

    Serial.println("Waiting for time to be set...");
    while (time(nullptr) < 1510644967)
    {
      delay(10);
    }
    Serial.println("Time set successfully.");

    Serial.println("Writing time...");
    cursor_x = 5;
    cursor_y = 20;
    String time = getTime();
    write_mode((GFXfont *)&roboto, &time[0], &cursor_x, &cursor_y, framebuffer, BLACK_ON_WHITE, &props);

    uint16_t v = analogRead(BATT_PIN);
    float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    battery(battery_voltage, framebuffer);

    epd_init();
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();

    Serial.println("Display updated and powered off.");

    Serial.println("Preparing for deep sleep...");
    esp_sleep_enable_timer_wakeup(30 * minutes);
    Serial.println("Entering deep sleep mode.");
    esp_deep_sleep_start();
  }
  else
  {
    Serial.println("Could not connect to saved network");

    preferences.begin("wifi", false);
    String savedSSID = preferences.getString("ssid", "");
    preferences.end();

    if (savedSSID != "")
    {
      Serial.println("Could no connect to wifi.");
      esp_sleep_enable_timer_wakeup(5 * minutes);
      Serial.println("Entering deep sleep mode.");
      esp_deep_sleep_start();
    }

    String ip = setupAP();

    // Allocate and initialize framebuffer
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer)
    {
      Serial.println("alloc memory failed !!!");
      while (1)
        ;
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    FontProperties props = {.fg_color = 0, .bg_color = 15, .fallback_glyph = 0, .flags = 0};

    // Display WiFi setup and cryptocurrency selection instructions
    displayInstructions(&props, ip, framebuffer);

    // Initialize ePaper display
    epd_init();
    epd_poweron();
    epd_clear_area_cycles(epd_full_screen(), 2, 50);
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();

    // Prevents cores from panicking while server is listening
    esp_task_wdt_init(30, false);
    esp_task_wdt_add(nullptr);
    esp_task_wdt_delete(nullptr);

    // Configure web server routes
    configureWebServerRoutes(server);

    // Start web server
    server.begin();
  }
}

void loop()
{
}
