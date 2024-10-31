/*
* M5NanoC6 Timezones.ino
* by @PaulskPt (Github)
* 2024-10-23
* License: MIT
*/
#include <M5NanoC6.h>
#include <M5UnitOLED.h>
#include <Unit_RTC.h>
#include <esp_sntp.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <stdlib.h>   // for putenv
#include <time.h>
#include <DateTime.h> // See: /Arduino/libraries/ESPDateTime/src
#include <Adafruit_NeoPixel.h>
#include "secret.h"
#include <map>
#include <memory>
#include <array>
#include <string>
#include <tuple>
#include <iomanip>  // For setFill and setW
#include <cstring>  // For strcpy
#include <sstream>  // Used in intToHex() (line 607)

// namespace {

#define CONFIG_LWIP_SNTP_UPDATE_DELAY (15 * 60 * 1000)  // 15 minutes

M5UnitOLED display (2, 1, 400000, 0, 0x3c); // Create an instance of the M5UnitOLED class

M5Canvas canvas(&display);

#define NTP_SERVER1   SECRET_NTP_SERVER_1 // "0.pool.ntp.org"

bool lStart = true;
bool sync_time = false;
time_t last_time_sync_epoch = 0; // see: time_sync_notification_cb()
//tm RTCdate;

// Different versions of the framework have different SNTP header file names and availability.

#if __has_include (<esp_sntp.h>)
  #include <esp_sntp.h>
  #define SNTP_ENABLED 1
#elif __has_include (<sntp.h>)
  #include <sntp.h>
  #define SNTP_ENABLED 1
#endif

#ifndef SNTP_ENABLED
#define SNTP_ENABLED 0
#endif

Unit_RTC RTC(0x51); // Create an instance of the Unit_RTC class

/* See: https://github.com/espressif/arduino-esp32/blob/master/variants/m5stack_nanoc6/pins_arduino.h */
#define M5NANO_C6_BLUE_LED_PIN     7  // D4

Adafruit_NeoPixel strip(1, M5NANO_C6_RGB_LED_DATA_PIN,
                        NEO_GRB + NEO_KHZ800);

int zone_idx; // Will be incremented in loop()
static constexpr const int nr_of_zones = SECRET_NTP_NR_OF_ZONES[0] - '0';  // Assuming SECRET_NTP_NR_OF_ZONES is defined as a string

std::map<int, std::tuple<std::string, std::string>> zones_map;

//} // end-of-namespace

// Function prototype (to prevent error 'rgb_led_wheel' was not declared in this scope)
void rgb_led_wheel(bool);

void create_maps() 
{
  
  for (int i = 0; i < nr_of_zones; ++i)
  {
    // Building variable names dynamically isn't directly possible, so you might want to define arrays instead
    switch (i)
    {
      case 0:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE0, SECRET_NTP_TIMEZONE0_CODE);
        break;
      case 1:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE1, SECRET_NTP_TIMEZONE1_CODE);
        break;
      case 2:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE2, SECRET_NTP_TIMEZONE2_CODE);
        break;
      case 3:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE3, SECRET_NTP_TIMEZONE3_CODE);
        break;
      case 4:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE4, SECRET_NTP_TIMEZONE4_CODE);
        break;
      case 5:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE5, SECRET_NTP_TIMEZONE5_CODE);
        break;
      case 6:
        zones_map[i] = std::make_tuple(SECRET_NTP_TIMEZONE6, SECRET_NTP_TIMEZONE6_CODE);
        break;
      default:
        break;
    }             
  }
}

/* Show or remove NTP Time Sync notification on the middle of the top of the display */
void ntp_sync_notification_txt(bool show)
{
  if (show)
    rgb_led_wheel(false); // blink cycle the RGB Led as an sync notification, but don't output text about RGB colors
  else
    canvas.fillRect(0, 0, display.width()-1, 55, BLACK);
}

/* Code fragment created with assistance of MS CoPilot */
// The SNTP callback function
void time_sync_notification_cb(struct timeval *tv) {
  // Get the current time  (very important!)
  time_t currentTime = time(nullptr);
  // Convert time_t to GMT struct tm
  struct tm* gmtTime = gmtime(&currentTime);
  uint16_t diff_t;
  // Set the last sync epoch time if not set
  if ((last_time_sync_epoch == 0) && (currentTime > 0))
    last_time_sync_epoch = currentTime;
  
  if (currentTime > 0) {   
    diff_t = currentTime - last_time_sync_epoch;
    last_time_sync_epoch = currentTime;
    #define CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS   CONFIG_LWIP_SNTP_UPDATE_DELAY / 1000

    if ((diff_t >= CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS) || lStart) {
      sync_time = true; // See loop initTime
      ntp_sync_notification_txt(true);
    }
  }
}

/* End of code created with assistance of MS CoPilot */

void esp_sntp_initialize() {
   if (esp_sntp_enabled()) { 
    esp_sntp_stop();  // prevent initialization error
  }
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, NTP_SERVER1);
  esp_sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb); // Set the notification callback function
  esp_sntp_init();

  // check the set sync_interval
  uint32_t rcvd_sync_interval_msecs = esp_sntp_get_sync_interval();
  Serial.print(F("SNTP sync interval rcvd fm server: "));
  Serial.print(rcvd_sync_interval_msecs/60000);
  Serial.println(F(" minutes"));
}

void setTimezone(void) {
    char elem_zone_code[50];
    strcpy(elem_zone_code, std::get<1>(zones_map[zone_idx]).c_str());
    setenv("TZ", elem_zone_code, 1);
    tzset();
}

bool initTime(void) {
  char elem_zone[50];
  char my_tz_code[50];
  bool ret = false;
  static constexpr const char NTP_SERVER2[] PROGMEM = "1.pool.ntp.org";
  static constexpr const char NTP_SERVER3[] PROGMEM = "2.pool.ntp.org";

  strcpy(elem_zone, std::get<0>(zones_map[zone_idx]).c_str());
  strcpy(my_tz_code, getenv("TZ"));

  configTzTime(my_tz_code, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  struct tm my_timeinfo;
  while (!getLocalTime(&my_timeinfo, 1000)) {
      delay(1000);
  }

  if (my_timeinfo.tm_sec != 0 || my_timeinfo.tm_min  != 0 || my_timeinfo.tm_hour  != 0 || 
      my_timeinfo.tm_mday != 0 || my_timeinfo.tm_mon  != 0 || my_timeinfo.tm_year  != 0 || 
      my_timeinfo.tm_wday != 0 || my_timeinfo.tm_yday != 0 || my_timeinfo.tm_isdst != 0) {
      setTimezone();
      ret = true;
  }
  return ret;
}

/*
  The settimeofday function is used to set the system’s date and time. 
  In the context of microcontrollers like the ESP32, it can be used 
  to set the time manually or after retrieving it from an NTP server.
*/
void setTime(int yr, int month, int mday, int hr, int minute, int sec, int isDst)
{
  struct tm tm;

  tm.tm_year = yr - 1900;   // Set date
  tm.tm_mon = month-1;
  tm.tm_mday = mday;
  tm.tm_hour = hr;      // Set time
  tm.tm_min = minute;
  tm.tm_sec = sec;
  tm.tm_isdst = isDst;  // 1 or 0

  time_t t = mktime(&tm);
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
}

bool set_RTC(void)
{
  bool ret = false;
  rtc_time_type RTCtime;
  rtc_date_type RTCdate;

  struct tm my_timeinfo;
  while (!getLocalTime(&my_timeinfo, 1000)) {
    delay(1000);
  }
  if (my_timeinfo.tm_year + 1900 > 1900)
  {
    ret = true;
    RTCtime.Hours   = my_timeinfo.tm_hour;
    RTCtime.Minutes = my_timeinfo.tm_min;
    RTCtime.Seconds = my_timeinfo.tm_sec;

    RTCdate.Year    = my_timeinfo.tm_year + 1900;
    RTCdate.Month   = my_timeinfo.tm_mon + 1;
    RTCdate.Date    = my_timeinfo.tm_mday;
    RTCdate.WeekDay = my_timeinfo.tm_wday;  // 0 = Sunday, 1 = Monday, etc.

    RTC.setDate(&RTCdate);
    RTC.setTime(&RTCtime);
  }
  return ret;
}

void disp_data(void) {
    struct tm my_timeinfo;
    if (!getLocalTime(&my_timeinfo)) return;
    
    canvas.clear();
    int scrollstep = 2;
    int32_t cursor_x = canvas.getCursorX() - scrollstep;
    if (cursor_x <= 0) {
        cursor_x = display.width();
    }
    char elem_zone[50];
    strncpy(elem_zone, std::get<0>(zones_map[zone_idx]).c_str(), sizeof(elem_zone) - 1);
    elem_zone[sizeof(elem_zone) - 1] = '\0'; // Ensure null termination

    char part1[20], part2[20], part3[20], part4[20];
    char copiedString[50], copiedString2[50];

    memset(part1, 0, sizeof(part1));
    memset(part2, 0, sizeof(part2));
    memset(part3, 0, sizeof(part3));
    memset(part4, 0, sizeof(part4));
    memset(copiedString, 0, sizeof(copiedString));
    memset(copiedString2, 0, sizeof(copiedString2));
    
    char *index1 = strchr(elem_zone, '/');  // index to the 1st occurrance of a forward slash (e.g.: Europe/Lisbon)
    char *index2 = nullptr; 
    char *index3 = strchr(elem_zone, '_'); // index to the occurrance of an underscore character (e.g.: Sao_Paulo)
    int disp_data_view_delay = 1000;

    strncpy(copiedString, elem_zone, sizeof(copiedString) - 1);
    copiedString[sizeof(copiedString) - 1] = '\0'; // Ensure null termination
    // Check if index1 is valid and within bounds
    if (index1 != nullptr) {
      size_t idx1_pos = index1 - elem_zone;
      if (idx1_pos < sizeof(copiedString)) {
        strncpy(part1, copiedString, idx1_pos);
        part1[idx1_pos] = '\0';
      }
      strncpy(copiedString2, index1 + 1, sizeof(copiedString2) - 1);
      copiedString2[sizeof(copiedString2) - 1] = '\0'; // Ensure null termination
      if (index3 != nullptr) {
        // Replace underscores with spaces in copiedString
        for (int i = 0; i < sizeof(copiedString2); i++) {
          if (copiedString2[i] == '_') {
            copiedString2[i] = ' ';
          }
        }
      }
      index2 = strchr(copiedString2, '/'); // index to the 2nd occurrance of a forward slahs (e.g.: America/Kentucky/Louisville)
      if (index2 != nullptr) {
        size_t idx2_pos = index2 - copiedString2;
        if (idx2_pos < sizeof(copiedString2)) {
            strncpy(part3, copiedString2, idx2_pos);  // part3, e.g.: "Kentucky"
            part3[idx2_pos] = '\0';
        }
        strncpy(part4, index2 + 1, sizeof(part4) - 1);  // part4, e.g.: "Louisville"
        part4[sizeof(part4) - 1] = '\0'; // Ensure null termination
      } else {
        strncpy(part2, copiedString2, sizeof(part2) - 1);
        part2[sizeof(part2) - 1] = '\0'; // Ensure null termination
      }
    }
       // =========== 1st view =================
    if (ck_BtnA()) return;

    if (index1 != nullptr && index2 != nullptr) {
        canvas.setCursor(0, 5);
        canvas.print(part1);
        canvas.setCursor(0, 28);
        canvas.print(part3);
        canvas.setCursor(0, 50);
        canvas.print(part4);
    } else if (index1 != nullptr){
        canvas.setCursor(0, 5);
        canvas.print(part1);
        canvas.setCursor(0, 30);
        canvas.print(part2);
    } else {
        canvas.setCursor(0, 5);
        canvas.print(copiedString);
    }
    display.waitDisplay();
    canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
    delay(disp_data_view_delay);
    // =========== 2nd view =================
    if (ck_BtnA()) return;
    canvas.clear();
    canvas.setCursor(0, 5);
    canvas.print("Zone");
    canvas.setCursor(0, 30);
    canvas.print(&my_timeinfo, "%Z %z");
    display.waitDisplay();
    canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
    delay(disp_data_view_delay);
    // =========== 3rd view =================
    if (ck_BtnA()) return;
    canvas.clear();
    canvas.setCursor(0, 5);
    canvas.print(&my_timeinfo, "%A");
    canvas.setCursor(0, 28);
    canvas.print(&my_timeinfo, "%B %d");
    canvas.setCursor(0, 50);
    canvas.print(&my_timeinfo, "%Y");
    display.waitDisplay();
    canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
    delay(disp_data_view_delay);
    // =========== 4th view =================
    if (ck_BtnA()) return;
    canvas.clear();
    canvas.setCursor(0, 28);
    canvas.print(&my_timeinfo, "%H:%M:%S local");
    /*
    canvas.setCursor(0, 50);
    if (index2 != nullptr) 
      canvas.printf("in %s\n", part4);
    else if (index1 != nullptr)
      canvas.printf("in %s\n", part2);
    */
    display.waitDisplay();
    canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
    delay(disp_data_view_delay);
}


void disp_msg(String str, int msg_delay = 6000)
{
  textdatum_t tdatum_old = canvas.getTextDatum(); // remember old TextDatum
  canvas.fillScreen(TFT_BLACK);
  //canvas.setBrightness(200);  // Make more brightness than normal
  canvas.clear();
  canvas.setTextDatum(middle_center);
  canvas.drawString(str, display.width() / 2, display.height() / 2);
  display.waitDisplay();
  canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
  delay(msg_delay);
  canvas.fillScreen(TFT_BLACK);
  //canvas.setBrightness(disp_brightness); // Restore brightness to normal
  canvas.setTextDatum(tdatum_old); // restore old TextDatum
  canvas.clear();
}

bool connect_WiFi(void)
{
  bool ret = false;
  #define WIFI_SSID            SECRET_SSID // "YOUR WIFI SSID NAME"
  #define WIFI_PASSWORD        SECRET_PASS // "YOUR WIFI PASSWORD"
  #define WIFI_MOBILE_SSID     SECRET_MOBILE_SSID
  #define WIFI_MOBILE_PASSWORD SECRET_MOBILE_PASS

  
  /*
  MS CoPilot
  In this case, since char buffer[50]; is a stack-allocated array, 
  it will automatically be removed from memory when it goes out of scope 
  (e.g., when the function it's defined in returns).
  You don't need to manually delete or free it.
  */
  char buffer[50]; // Make sure this buffer is large enough

  static constexpr const char* txts[] PROGMEM = {
    "WiFi ",            // 0
    "connecting to ",   // 1
    "mobile ",          // 2
    "phone",            // 3
    "fixed ",           // 4
    "AP",               // 5
    "OK"                // 6
  };

  Serial.printf("%s%s%s%s\n", txts[0], txts[1], txts[2], txts[3]); // "WiFi connecting to mobile phone "
  
  /* First try connect to mobile phone */
  WiFi.begin(WIFI_MOBILE_SSID, WIFI_MOBILE_PASSWORD);
  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
  {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
    ret = true;
    Serial.printf("%s%s%s\n", txts[0], txts[2], txts[6]); // "WiFi mobile OK"
  
    buffer[0] = '\0'; // Initialize buffer to empty string
    strcat(buffer, (char*)txts[0]); // Ensure pointer is correctly accessed
    strcat(buffer, (char*)txts[2]);
    strcat(buffer, (char*)txts[5]);
    disp_msg(buffer, 3000);
  }
  else
  {
    /* Then try WiFi fixed (for example: home) */
    Serial.printf("%s%s%s%s\n", txts[0], txts[1], txts[4], txts[5]); // "WiFi connecting to fixed AP"
    WiFi.begin( WIFI_SSID, WIFI_PASSWORD );
    for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
    {
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) 
    {
      ret = true;
      Serial.printf("%s%s%s\n", txts[0], txts[4], txts[6]); // "WiFi fixed OK"
      buffer[0] = '\0'; // Initialize buffer to empty string
      strcat(buffer, (char*)txts[0]); // Ensure pointer is correctly accessed
      strcat(buffer, (char*)txts[4]);
      strcat(buffer, (char*)txts[6]);
      disp_msg(buffer, 3000);
    }
  }
  return ret;
}

bool ck_BtnA(void)
{
  bool buttonPressed = false;
  NanoC6.update();
  if (NanoC6.BtnA.wasPressed())  // 100 mSecs
    buttonPressed = true;
  return buttonPressed;
}

void blink_blue_led(void)
{
  #define blue_delay_time  300
  digitalWrite(M5NANO_C6_BLUE_LED_PIN, HIGH);
  delay(blue_delay_time);
  digitalWrite(M5NANO_C6_BLUE_LED_PIN, LOW);
  delay(blue_delay_time);
}

void rgb_led_wheel(bool pr_txt = false)
{
  const int  iColors[4][3] = 
  {
    {255,   0,   0},
    {  0, 255,   0},
    {  0,   0, 255},
    {  0,   0,   0}
  };
  int le = sizeof(iColors)/sizeof(iColors[0]);
  
  for (int i = 0; i < le; i++)
  {
    strip.setPixelColor(0, strip.Color(iColors[i][0], iColors[i][1], iColors[i][2]));
    strip.show();
    delay(500);
  }
}

void setup(void) 
{
  NanoC6.begin();

  // See: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/unit/oled.pdf
  display.begin();
  canvas.setColorDepth(1); // mono color

  if (display.isEPD())
  {
    //scrollstep = 16;
    display.setEpdMode(epd_mode_t::epd_fastest);
    display.invertDisplay(true);
    display.clear(TFT_BLACK);
  }
  display.setRotation(1);
  canvas.setFont(&fonts::FreeSans9pt7b); // was: efontCN_14);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.createSprite(display.width() + 64, 72);
  
  //Serial.println(F("\n\nM5Stack Atom Matrix with RTC unit and OLED display unit test."));
  static constexpr const char txt2[] PROGMEM = "M5NanoC6 Timezones with OLED and RTC units test.";
  Serial.print("\n\n");
  Serial.println(txt2);
  disp_msg("Timezones...", 3000);

  // See: https://fcc.report/FCC-ID/2AN3WM5NANOC6/7170758.pdf page 7/8
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  #define M5NANO_C6_RGB_LED_PWR_PIN  19 
  #define M5NANO_C6_RGB_LED_DATA_PIN 20

  pinMode(M5NANO_C6_BLUE_LED_PIN, OUTPUT);

  // Enable RGB LED Power
  pinMode(M5NANO_C6_RGB_LED_PWR_PIN, OUTPUT);
  digitalWrite(M5NANO_C6_RGB_LED_PWR_PIN, HIGH);

  strip.begin();
  strip.show();
  
  //Serial.begin(115200);  // This command is now done by M5NanoC6.begin()

  create_maps();  // creeate zones_map

  RTC.begin();

  delay(1000);

  if (connect_WiFi())
  {

    esp_sntp_initialize();  // name sntp_init() results in compilor error "multiple definitions sntp_init()"
    int status = esp_sntp_get_sync_status();
  }
  canvas.clear();
}

void loop(void)
{
  static constexpr const char txt1[] PROGMEM = "loop(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
  unsigned long zone_chg_start_t = millis();
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;
  
  int connect_try = 0;
  bool TimeToChangeZone = false;

  static constexpr const char* txts[] PROGMEM = {    // longest string 11 (incl \0)
    "Reset...",       //  0  was 21
    "Bye...",         //  1  was 22
    "RTC updated ",   //  2  was 1st part of 16
    "with SNTP ",     //  3  was 2nd part of 16
    "datetime",       //  4  was 3rd part of 16
    "Free heap: "     //  5
    };
  
  while (true)
  {
    if (ck_BtnA())
    {
      // We have a button press, so do a software reset
      disp_msg(txts[0]); // there is already a wait of 6000 in disp_msg()
      esp_restart();
    } else {

      if (WiFi.status() != WL_CONNECTED) // Check if we're still connected to WiFi
      {
        if (connect_WiFi())
          connect_try = 0;  // reset count
        else
          connect_try++;

        if (connect_try >= 10)
        {
          break;
        }
      }
      // sync_time is set in function: time_sync_notification_cb()
      if (sync_time || lStart) {
        if (sync_time) {
          if (initTime()) {
            if (set_RTC()) {
              sync_time = false;
              Serial.printf("%s%s%s\n", txts[2], txts[3], txts[4]);
              Serial.printf("%s%u\n", txts[5], ESP.getFreeHeap()); 
              heap_caps_print_heap_info(MALLOC_CAP_8BIT);
            }
          }
        }
      }

      zone_chg_curr_t = millis();

      zone_chg_elapsed_t = zone_chg_curr_t - zone_chg_start_t;

      /* Do a zone change */
      if (lStart || (zone_chg_elapsed_t >= zone_chg_interval_t))
      {
        if (lStart)
          zone_idx = -1; // will be increased in code below
        
        TimeToChangeZone = true;
        zone_chg_start_t = zone_chg_curr_t;
        digitalWrite(M5NANO_C6_BLUE_LED_PIN, HIGH);  // Switch on the Blue Led

        /*
        Increase the zone_index, so that the sketch
        will display data from a next timezone in the map: time_zones.
        */
        if (zone_idx < (nr_of_zones-1))
          zone_idx++;
        else
          zone_idx = 0;
        setTimezone();
        TimeToChangeZone = false;
        digitalWrite(M5NANO_C6_BLUE_LED_PIN, LOW);  // Switch off the Blue Led
        disp_data();
      }
    }
    disp_data();
    lStart = false;
    NanoC6.update();  // Read the press state of the key.
  } // end-of-while
  disp_msg(txts[1]);

  /* Go into an endless loop after WiFi doesn't work */
  do
  {
    delay(5000);
  } while (true);
}
