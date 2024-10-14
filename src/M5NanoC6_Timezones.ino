/* 
* Sketch for M5Stack M5NanoC6 device with a 1.3 inch OLED SH1107 display with 128 x 64 pixels and a RTC unit (8563)
* The OLED and RTC units are connected to the GROVE Port of a M5NanoC6 device via a GROVE HUB.
* by @PaulskPt 2024-10-13
* License: MIT.
*/
#include <M5GFX.h>
#include <M5UnitOLED.h>
#include <M5NanoC6.h>
#include <Unit_RTC.h>

#ifdef sntp_getoperatingmode
#undef sntp_getoperatingmode
#endif

//#include <esp_sntp.h>
#include <sntp.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <stdlib.h>   // for putenv
#include <time.h>
#include <DateTime.h> // See: /Arduino/libraries/ESPDateTime/src

#include <Adafruit_NeoPixel.h>
#include "secret.h"

#include <iostream>
#include <map>
#include <memory>
#include <array>
#include <string>
#include <tuple>
#include <iomanip> // For setFill and setW
#include <cstring> // For strcpy
#include <sstream>  // Used in ck_RFID()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <Wire.h>

namespace {

/* See: https://github.com/espressif/arduino-esp32/blob/master/variants/m5stack_nanoc6/pins_arduino.h */
#define SDA 2
#define SCL 1
#define I2C_FREQ 400000
#define I2C_PORT 0
#define I2C_ADDR_OLED 0x3c
#define I2C_ADDR_RTC  0x51

M5UnitOLED display (SDA, SCL, I2C_FREQ, I2C_PORT, I2C_ADDR_OLED);
M5Canvas canvas(&display);

int dw = display.width();
int dh = display.height();

#define WIFI_SSID     SECRET_SSID // "YOUR WIFI SSID NAME"
#define WIFI_PASSWORD SECRET_PASS //"YOUR WIFI PASSWORD"
#define NTP_TIMEZONE  SECRET_NTP_TIMEZONE // for example: "Europe/Lisbon"
#define NTP_TIMEZONE_CODE  SECRET_NTP_TIMEZONE_CODE // for example: "WET0WEST,M3.5.0/1,M10.5.0"
#define NTP_SERVER1   SECRET_NTP_SERVER_1 // "0.pool.ntp.org"
#define NTP_SERVER2   "1.pool.ntp.org"
#define NTP_SERVER3   "2.pool.ntp.org"

#ifdef CONFIG_LWIP_SNTP_UPDATE_DELAY   // Found in: Component config > LWIP > SNTP
#undef CONFIG_LWIP_SNTP_UPDATE_DELAY
#endif

#define CONFIG_LWIP_SNTP_UPDATE_DELAY  15 * 60 * 1000 // = 15 minutes (15 seconds is the minimum). Original setting: 3600000  // 1 hour

std::string elem_zone;
std::string elem_zone_code;
std::string elem_zone_code_old;
bool zone_has_changed = false;

bool my_debug = false;
bool lStart = true;
bool use_local_time = false; // for the external RTC    (was: use_local_time = true // for the ESP32 internal clock )
struct tm timeinfo;
bool use_timeinfo = true;
std::tm* tm_local = {};
//tm RTCdate;

// 128 x 64
static constexpr const int hori[] = {0, 30, 50};
static constexpr const int vert[] = {0, 30, 60, 90, 120};

unsigned long start_t = millis();
uint8_t FSM = 0;  // Store the number of key presses
int connect_try = 0;
int max_connect_try = 10;

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


Unit_RTC RTC(I2C_ADDR_RTC);

rtc_time_type RTCtime;
rtc_date_type RTCdate;

char str_buffer[64];

#define M5NANO_C6_BLUE_LED_PIN     7  // D4
#define M5NANO_C6_BTN_PIN          9  // D6
#define M5NANO_C6_IR_TX_PIN        3  // A3
#define M5NANO_C6_RGB_LED_PWR_PIN  19 
#define M5NANO_C6_RGB_LED_DATA_PIN 20

#define NUM_LEDS 1

Adafruit_NeoPixel strip(NUM_LEDS, M5NANO_C6_RGB_LED_DATA_PIN,
                        NEO_GRB + NEO_KHZ800);

// For unitOLED
int textpos    = 0;
int scrollstep = 2;
int32_t cursor_x = canvas.getCursorX() - scrollstep;

volatile bool buttonPressed = false;

#define rgb_delay_time   500  // was: 1000
#define blue_delay_time  300

const char* boardName;
static constexpr const char* wd[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
char text[50];
size_t textlen = 0;

unsigned long zone_chg_start_t = millis();
bool TimeToChangeZone = false;

int zone_idx = 0;
const int zone_max_idx = 6;
/*
std::string zones[zone_max_idx] = {"Europe/Lisbon", "America/Kentucky/Louisville", "America/New_York", "America/Sao_Paulo", "Europe/Amsterdam", "Australia/Sydney"};
std::string zone_code[zone_max_idx] = {"WET0WEST,M3.5.0/1,M10.5.0", "EST5EDT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0", "<-03>3", "CET-1CEST,M3.5.0,M10.5.0/3", "AEST-10AEDT,M10.1.0,M4.1.0/3" };
*/

std::map<int, std::tuple<std::string, std::string>> zones_map;

} // end-of-namespace

// Function prototype (to prevent error 'rgb_led_wheel' was not declared in this scope)
void rgb_led_wheel(bool);

void create_maps(void) 
{
  zones_map[0] = std::make_tuple("Asia/Tokyo", "JST-9");
  zones_map[1] = std::make_tuple("America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0");
  zones_map[2] = std::make_tuple("America/New_York", "EST5EDT,M3.2.0,M11.1.0");
  zones_map[3] = std::make_tuple("America/Sao_Paulo", "<-03>3");
  zones_map[4] = std::make_tuple("Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3");
  zones_map[5] = std::make_tuple("Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3");

  // Iterate and print the elements
  /*
  std::cout << "create_maps(): " << std::endl;
  for (const auto& pair : zones_map)
  {
    std::cout << "Key: " << pair.first << ". Values: ";
    std::cout << std::get<0>(pair.second) << ", ";
    std::cout << std::get<1>(pair.second) << ", ";
    std::cout << std::endl;
  }
  */
}

void map_replace_first_zone(void)
{
  bool ret = false;
  int tmp_zone_idx = 0;
  std::string elem_zone_original;
  std::string elem_zone_code_original;
  elem_zone  = std::get<0>(zones_map[tmp_zone_idx]);
  elem_zone_code  = std::get<1>(zones_map[tmp_zone_idx]);
  std::string elem_zone_check;
  std::string elem_zone_code_check;
  
  elem_zone_original = elem_zone; // make a copy
  elem_zone_code_original = elem_zone_code;
  elem_zone = NTP_TIMEZONE;
  elem_zone_code = NTP_TIMEZONE_CODE;
  zones_map[0] = std::make_tuple(elem_zone, elem_zone_code);
  // Check:
  elem_zone_check  = std::get<0>(zones_map[tmp_zone_idx]);
  elem_zone_code_check  = std::get<1>(zones_map[tmp_zone_idx]);

  /*
  std::cout << "Map size before erase: " << myMap.size() << std::endl;

  for (int i = 0; i < 2; i++
  {
    // Get iterator to the element with key i
    auto it = zones_map.find(i);
    if (it != zones_map.end())
    {
        zones_map.erase(it);
    }
  std::cout << "Map size after erase: " << myMap.size() << std::endl;
  */
 
  if (my_debug)
  {
    std::cout << "map_replace_first_zone(): successful replaced the first record of the zone_map:" << std::endl;
    
    std::cout << "zone original: \"" << elem_zone_original.c_str() << "\""
      << ", replaced by zone: \"" << elem_zone_check.c_str()  << "\""
      << " (from file secrets.h)" 
      << std::endl;
    
    std::cout << "zone code original: \"" <<  elem_zone_code_original.c_str() << "\""
      << ", replaced by zone code: \"" << elem_zone_code_check.c_str() << "\""
      << std::endl;
  }
}



/* Show or remove NTP Time Sync notification on the middle of the top of the display */
void ntp_sync_notification_txt(bool show)
{
  if (show)
  {
    //std::shared_ptr<std::string> TAG = std::make_shared<std::string>("mtp_sync_notification_txt(): ");
    //const char txt[] = "beep command to the M5 Atom Echo device";
    rgb_led_wheel(false); // blink cycle the RGB Led as an sync notification, but don't output text about RGB colors
  }
  else
  {
    //canvas.fillRect(dw/2-25, 15, 50, 25, BLACK);
    canvas.fillRect(0, 0, dw-1, 55, BLACK);
  }
}

bool is_tm_empty(const std::tm& timeinfo)
{
  return timeinfo.tm_sec == 0 && timeinfo.tm_min  == 0 && timeinfo.tm_hour  == 0 &&
        timeinfo.tm_mday == 0 && timeinfo.tm_mon  == 0 && timeinfo.tm_year  == 0 &&
        timeinfo.tm_wday == 0 && timeinfo.tm_yday == 0 && timeinfo.tm_isdst == 0;
}

void time_sync_notification_cb(struct timeval *tv)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("time_sync_notification_cb(): ");
  if (my_debug)
  {
    if (tv != nullptr) 
    {
      std::cout << *TAG << "Parameter *tv, tv-<tv_sec (Seconds): " << 
        tv->tv_sec << ", tv->tv_usec (microSeconds): " << tv->tv_usec << std::endl;
    } 
    else 
    {
      std::cerr << *TAG << "Invalid timeval pointer" << std::endl;
    }
  }
  std::cout << *TAG << "calling initTime()" << std::endl;
  if (initTime())
  {
    time_t t = time(NULL);
    std::cout << *TAG << "time synchronized at time (UTC): " << asctime(gmtime(&t)) << std::flush;  // prevent a 2nd LF. Do not use std::endl
    ntp_sync_notification_txt(true);
  }
}

void sntp_initialize()
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("sntp_initialize(): ");
  if (sntp_enabled()) { 
    sntp_stop();  // prevent initialization error
  }
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, NTP_SERVER1);
  sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  sntp_init();
  
  if (my_debug)
  {
    std::cout << *TAG << "sntp initialized" << std::endl;
    std::cout << *TAG << "sntp set to polling mode" << std::endl;
  }
  std::cout << *TAG << "sntp polling interval: " << 
    std::to_string(CONFIG_LWIP_SNTP_UPDATE_DELAY/60000) << " Minute(s)" << std::endl;
}

void setTimezone(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("setTimezone(): ");
  elem_zone = std::get<0>(zones_map[zone_idx]);
  elem_zone_code = std::get<1>(zones_map[zone_idx]);
  if (elem_zone_code != elem_zone_code_old)
  {
    elem_zone_code_old = elem_zone_code;
    const char s1[] = "has changed to: ";
    zone_has_changed = true;
    //std::cout << *TAG << "Sending cmd to beep to Atom Echo" << std::endl;
    //send_cmd_to_AtomEcho(); // Send a digital signal to the Atom Echo to produce a beep
    if (my_debug)
    {
      std::cout << *TAG << "Timezone " << s1 << "\"" << elem_zone.c_str() << "\"" << std::endl;
      std::cout << *TAG << "Timezone code " << s1 << "\"" << elem_zone_code.c_str() << "\"" << std::endl;
    }
  }
  /*
    See: https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32/api-reference/system/system_time.html#sntp-time-synchronization
    Call setenv() to set the TZ environment variable to the correct value based on the device location. 
    The format of the time string is the same as described in the GNU libc documentation (although the implementation is different).
    Call tzset() to update C library runtime data for the new timezone.
  */
  // std::cout << *TAG << "Setting Timezone to \"" << (elem_zone_code.c_str()) << "\"\n" << std::endl;
  setenv("TZ",elem_zone_code.c_str(),1);
  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  delay(500);
  tzset();
  delay(500);

  if (my_debug)
  {
    // Check:
    std::cout << *TAG << "check environment variable TZ = \"" << getenv("TZ") << "\"" << std::endl;
  }
}

/*
  The getLocalTime() function is often used in microcontroller projects, such as with the ESP32, 
  to retrieve the current local time from an NTP (Network Time Protocol) server. 
  Here’s a simple example of how you can use this function with the ESP32:
*/
bool poll_NTP()
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("poll_NTP(): ");
  bool ret = false;

  if(getLocalTime(&timeinfo))
  {
    std::cout << "getLocalTime(&timeinfo): timeinfo = " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
    ret = true;
  }
  else
  {
    std::cout << *TAG << "Failed to obtain time " << std::endl;
    canvas.clear();
    canvas.setCursor(hori[0], vert[2]);
    canvas.print("Failed to obtain time");
    display.waitDisplay();
    canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
    delay(3000);
  }
  return ret;
}

/*
bool initTime(void)
{
  bool ret = false;
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("initTime(): ");
  elem_zone = std::get<0>(zones_map[zone_idx]);
  elem_zone_code = std::get<1>(zones_map[zone_idx]);

  if (my_debug)
  {
    std::cout << *TAG << "Setting up time" << std::endl;
    std::cout << "zone       = \"" << elem_zone.c_str() << "\"" << std::endl;
    std::cout << "zone code  = \"" << elem_zone_code.c_str() << "\"" << std::endl;
    std::cout 
      << "NTP_SERVER1 = \"" << NTP_SERVER1 << "\", " 
      << "NTP_SERVER2 = \"" << NTP_SERVER2 << "\", "
      << "NTP_SERVER3 = \"" << NTP_SERVER3 << "\""
      << std::endl;
  }
  // Init and get the time
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // printLocalTime();
}
*/

bool initTime(void)
{
  bool ret = false;
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("initTime(): ");
  elem_zone = std::get<0>(zones_map[zone_idx]);
  //elem_zone_code = std::get<1>(zones_map[zone_idx]);

  /*
  * See answer from: bperrybap (March 2021, post #6)
  * on: https://forum.arduino.cc/t/getting-time-from-ntp-service-using-nodemcu-1-0-esp-12e/702333/5
  */

#ifndef ESP32
#define ESP32 (1)
#endif

//=========== UNDER TEST ON 2024-10-14 ====================================================
std::string my_tz_code = getenv("TZ");

if (my_debug)
{
  std::cout << *TAG << "Setting up time" << std::endl;
  std::cout << "zone                 = \"" << elem_zone.c_str() << "\"" << std::endl;
  //std::cout << "elem_zone_code       = \"" << elem_zone_code.c_str() << "\"" << std::endl;
  std::cout << "getenv(\"TZ\") code    = \"" << my_tz_code.c_str() << "\"" << std::endl;
  std::cout 
    << "NTP_SERVER1 = \"" << NTP_SERVER1 << "\", " 
    << "NTP_SERVER2 = \"" << NTP_SERVER2 << "\", "
    << "NTP_SERVER3 = \"" << NTP_SERVER3 << "\""
    << std::endl;
}

// See: /Arduino/libraries/ESPDateTime/src/DateTime.cpp, lines 76-80
#if defined(ESP8266)
  configTzTime(elem_zone_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3); 
#elif defined(ESP32)
  std::cout << *TAG << "Setting configTzTime to: \"" << my_tz_code.c_str() << "\"" << std::endl;
  // configTime(0, 3600, NTP_SERVER1);
  configTzTime(my_tz_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);  // This one is use for the M5Stack Atom Matrix
#endif

//=========== END TEST ON 2024-10-14 ======================================================

  while (!getLocalTime(&timeinfo, 1000))
  {
    std::cout << "." << std::flush;
    delay(1000);
  };

  std::cout << *TAG << "NTP Connected. " << std::endl;

  if (is_tm_empty(timeinfo))
  {
    std::cout << *TAG << "Failed to obtain datetime from NTP" << std::endl;
  }
  else
  {
    if (my_debug)
    {
      std::cout << *TAG << "Got this datetime from NTP: " << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << std::endl;
    }
    // Now we can set the real timezone
    setTimezone();

    ret = true;
  }
  return ret;
}









/*
    The settimeofday function is used to set the system’s date and time. 
    In the context of microcontrollers like the ESP32, it can be used 
    to set the time manually or after retrieving it from an NTP server.
    Here’s an example of how you might use it:
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
  std::cout << "Setting time: " << (asctime(&tm)) << std::endl;
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
}

bool set_RTC(void)
{
  bool ret = false;
  constexpr char s[] = "\nset_RTC(): external RTC ";
  // Serial.print("set_RTC(): timeinfo.tm_year = ");
  // Serial.println(timeinfo.tm_year);
  if (timeinfo.tm_year + 1900 > 1900)
  {
    RTCtime.Hours   = timeinfo.tm_hour;
    RTCtime.Minutes = timeinfo.tm_min;
    RTCtime.Seconds = timeinfo.tm_sec;

    RTCdate.Year    = timeinfo.tm_year + 1900;
    RTCdate.Month   = timeinfo.tm_mon + 1;
    RTCdate.Date    = timeinfo.tm_mday;
    RTCdate.WeekDay = timeinfo.tm_wday;  // 0 = Sunday, 1 = Monday, etc.

    RTC.setDate(&RTCdate);
    RTC.setTime(&RTCtime);
    std::cout << "set_RTC(): external RTC has been set" << std::endl;
    //std::cout << "Check: " << std::endl;
    /*
    poll_date_RTC();
    poll_time_RTC();
    */
    
    /*
    if (my_debug)
    {
      poll_RTC();
      ret = true;
    }
    */
  }
  return ret;
}

void poll_RTC(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("poll_RTC(): ");
  time_t t = time(NULL);
  delay(500);

  /// ESP32 internal timer
  // struct tm timeinfo;
  t = std::time(nullptr);

  if (!my_debug)
  {
    std::tm* tm = std::gmtime(&t);  // for UTC.
    std::cout << std::dec << *TAG << "ESP32 UTC : " 
      << std::setw(4) << (tm->tm_year+1900) << "-"
      << std::setfill('0') << std::setw(2) << (tm->tm_mon+1) << "-"
      << std::setfill('0') << std::setw(2) << (tm->tm_mday) << " ("
      << wd[tm->tm_wday] << ") "
      << std::setfill('0') << std::setw(2) << (tm->tm_hour) << ":"
      << std::setfill('0') << std::setw(2) << (tm->tm_min)  << ":"
      << std::setfill('0') << std::setw(2) << (tm->tm_sec)  << std::endl;
  }

  if (!my_debug)
  {
    // std::tm* tm_local  Global var!
    //tm_local = std::localtime(&t);  // for local timezone.
    tm_local = localtime(&t);
    elem_zone = std::get<0>(zones_map[zone_idx]);
    std::cout << std::dec << *TAG << "ESP32 " << elem_zone             << ": " 
      << std::setw(4)                      << (tm_local->tm_year+1900) << "-"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_mon+1)     << "-"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_mday)      << " ("
      << wd[tm_local->tm_wday] << ") "
      << std::setfill('0') << std::setw(2) << (tm_local->tm_hour)      << ":"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_min)       << ":"
      << std::setfill('0') << std::setw(2) << (tm_local->tm_sec) << std::endl;
  }
}

void printLocalTime()
{ 
  // Serial.print("printLocalTimer(): timeinfo.tm_year = ");
  // Serial.println(timeinfo.tm_year);
  struct tm my_timeinfo;
  if (getLocalTime(&my_timeinfo)) // update local time
  {
    if (my_timeinfo.tm_year + 1900 > 1900)
    {
      elem_zone = std::get<0>(zones_map[zone_idx]);
      // Serial.println(&my_timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
      std:: cout << elem_zone.c_str() << ", " << std::put_time(&my_timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ") << std::endl;
    }
  }
}

void disp_data(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("disp_data(): ");
  canvas.clear();
  cursor_x = canvas.getCursorX() - scrollstep;
  if (cursor_x <= 0)
  {
    textpos  = 0;
    cursor_x = display.width();
  }

  if (!getLocalTime(&timeinfo)) // update local time
  {
    std::cout << *TAG << "failed to get local time" << std::endl;
    return;
  }  

  elem_zone  = std::get<0>(zones_map[zone_idx]);
  std::string copiedString, copiedString2;
  std::string part1, part2, part3, part4;
  std::string partUS1, partUS2;
  int index, index2, index3 = -1;
  copiedString =  elem_zone;
  index = copiedString.find('/');
  index3 = copiedString.find('_'); // e.g. in "New_York" or "Sao_Paulo"
  int disp_data_view_delay = 1000; // was: 2000
  if (index3 >= 0)
  {
    partUS1 = copiedString.substr(0, index3);
    partUS2 = copiedString.substr(index3+1);
    copiedString = partUS1 + " " + partUS2;  // replaces the found "_" by a space " "
  }
  if (index >= 0)
  {
    part1 = copiedString.substr(0, index);
    part2 = copiedString.substr(index+1);

    copiedString2 = part2.c_str();

    // Search for a second forward slash  (e.g.: "America/Kentucky/Louisville")
    index2 = copiedString2.find('/'); 
    if (index2 >= 0)
    {
      part3 = copiedString2.substr(0, index2);
      part4 = copiedString2.substr(index2+1);
    }
  }

  // =========== 1st view =================
  if (ck_BtnA())
    return;

  if (index >= 0 && index2 >= 0)
  {
    canvas.setCursor(hori[0], vert[0]+5);
    canvas.print(part1.c_str());
    canvas.setCursor(hori[0], vert[1]-2);
    canvas.print(part3.c_str());
    canvas.setCursor(hori[0], vert[2]-10);
    canvas.print(part4.c_str());
  }
  else if (index >= 0)
  {
    canvas.setCursor(hori[0], vert[0]+5);
    canvas.print(part1.c_str());
    canvas.setCursor(hori[0], vert[1]);
    canvas.print(part2.c_str());
  }
  else
  {
    canvas.setCursor(hori[0], vert[0]+5);
    canvas.print(copiedString.c_str());
  }

  display.waitDisplay();
  canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
  delay(disp_data_view_delay);
  // =========== 2nd view =================
  if (ck_BtnA())
    return;
  canvas.clear();
  // canvas.fillRect(0, vert[0], dw-1, dh-1, BLACK);
  canvas.setCursor(hori[0], vert[0]+5);
  canvas.print("Zone");
  canvas.setCursor(hori[0], vert[1]);
  canvas.print(&timeinfo, "%Z %z");
  display.waitDisplay();
  canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
  delay(disp_data_view_delay);
  // =========== 3rd view =================
  if (ck_BtnA())
    return;
  canvas.clear();
  canvas.setCursor(hori[0], vert[0]+5);
  canvas.print(&timeinfo, "%A");  // Day of the week
  canvas.setCursor(hori[0], vert[1]-2);
  canvas.print(&timeinfo, "%B %d");
  canvas.setCursor(hori[0], vert[2]-10);
  canvas.print(&timeinfo, "%Y");
  display.waitDisplay();
  canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
  delay(disp_data_view_delay);
  // =========== 4th view =================
  if (ck_BtnA())
    return;
  canvas.clear();
  canvas.setCursor(hori[0], vert[1]-2);
  canvas.print(&timeinfo, "%H:%M:%S local");
  display.waitDisplay();
  canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
  delay(disp_data_view_delay);
}

void disp_msg(String str)
{
  canvas.fillScreen(TFT_BLACK);
  //canvas.setBrightness(200);  // Make more brightness than normal
  canvas.clear();
  canvas.setTextDatum(middle_center);
  canvas.drawString(str, display.width() / 2, display.height() / 2);
  display.waitDisplay();
  canvas.pushSprite(&display, 0, (display.height() - canvas.height()) >> 1);
  delay(6000);
  canvas.fillScreen(TFT_BLACK);
  //canvas.setBrightness(disp_brightness); // Restore brightness to normal
  canvas.clear();
}

/*
bool connect_WiFi(void)
{
  bool ret = false;
  std::cout << "\nWiFi:" << std::endl;
  WiFi.begin( WIFI_SSID, WIFI_PASSWORD );

  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
  {
    std::cout << "." << std::flush;
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
    ret = true;
    std::cout << "\r\nWiFi Connected to: " << (WIFI_SSID) << std::endl;
    IPAddress ip;
    ip = WiFi.localIP();
    std::cout << "IP address: " << std::flush;
    std::string ipadd1 = std::string(WiFi.localIP().toString().c_str());
    std::cout << ipadd1 << std::endl;
    byte mac[6];
    WiFi.macAddress(mac);
    std::cout << "MAC: " << std::flush;
    for (int i = 0; i < 6; i++)
    {
      std::cout << std::hex << std::setw(2) << std::setfill('0') 
        << static_cast<int>(mac[i]) << std::flush;
      if (i < 5) 
        std::cout << ":" << std::flush;
    }
    std::cout << std::endl;
  }
  else
  {
    std::cout << "\r\nWiFi connection failed." << std::endl;
  }
  return ret;
}
*/

bool connect_WiFi(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("connect_WiFi(): ");
  bool ret = false;
  if (my_debug)
  {
    std::cout << std::endl << "WiFi: " << std::flush;
  }
  WiFi.begin( WIFI_SSID, WIFI_PASSWORD );

  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
  {
    if (my_debug)
      std::cout << "." << std::flush;
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
    ret = true;
    if (my_debug)
      std::cout << "\r\nWiFi Connected to: " << WIFI_SSID << std::endl;
    else
      std::cout << "\r\nWiFi Connected" << std::endl;

    if (my_debug)
    {
      IPAddress ip;
      ip = WiFi.localIP();
      // Convert IPAddress to string
      String ipStr = ip.toString();
      std::cout << "IP address: " << ipStr.c_str() << std::endl;

      byte mac[6];
      WiFi.macAddress(mac);

      // Allocate a buffer of 18 characters (12 for MAC + 5 colons + 1 null terminator)
      char* mac_buff = new char[18];

      // Create a shared_ptr to manage the buffer with a custom deleter
      std::shared_ptr<char> bufferPtr(mac_buff, customDeleter);

      std::cout << *TAG << std::endl;

      std::cout << "MAC: ";
      for (int i = 0; i < 6; ++i)
      {
        if (i > 0) std::cout << ":";
        std::cout << std::hex << (int)mac[i];
      }
      std::cout << std::dec << std::endl;
    }
  }
  else
  {
    std::cout << "\r\n" << "WiFi connection failed." << std::endl;
  }
  return ret;
}

void customDeleter(char* buffer) {
    // std::cout << "\nCustom deleter called\n" << std::endl;
    delete[] buffer;
}


void getID(void)
{
  uint64_t chipid_EfM = ESP.getEfuseMac(); // The chip ID is essentially the MAC address 
  char chipid[13] = {0};
  sprintf( chipid,"%04X%08X", (uint16_t)(chipid_EfM>>32), (uint32_t)chipid_EfM );
  std::cout << "\nESP32 Chip ID = " << chipid << "\n" << std::endl;
  std::cout << "chipid mirrored (same as M5Burner MAC): " << std::flush;
  // Mirror MAC address:
  for (uint8_t i = 10; i >= 0; i-=2)  // 10, 8. 6. 4. 2, 0
  {
    std::cout << (chipid[i])    // bytes 10, 8, 6, 4, 2, 0
    << (chipid[i+1])            // bytes 11, 9, 7. 5, 3, 1
    << std::flush;
    if (i > 0)
      std::cout << ":" << std::flush;
    if (i == 0)  // Note: this needs to be here. Yes, it is strange but without it the loop keeps on running.
      break;     // idem.
  }
}

bool ck_BtnA(void)
{
  NanoC6.update();
  //if (M5Dial.BtnA.isPressed())
  if (NanoC6.BtnA.wasPressed())  // 100 mSecs
    buttonPressed = true;
  else
    buttonPressed = false;
  return buttonPressed;
}

void blink_blue_led(void)
{
  digitalWrite(M5NANO_C6_BLUE_LED_PIN, HIGH);
  delay(blue_delay_time);
  digitalWrite(M5NANO_C6_BLUE_LED_PIN, LOW);
  delay(blue_delay_time);
}

void rgb_led_wheel(bool pr_txt = false)
{
  const char txt[] = "RGB Led set to: ";
  const char sColors[4][6] = {"RED", "GREEN", "BLUE", "BLACK"};
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
    /*
    canvas.scroll(-scrollstep, 0);
    canvas.clear();
    canvas.setCursor(hori[0], vert[0]+10);
    canvas.printf("%s", txt);
    canvas.setCursor(hori[0], vert[1]);
    canvas.printf("%s", sColors[i]);
    // Push the sprite to the display
    canvas.pushSprite(0, 0);
    */
    if (pr_txt)
      std::cout << (txt) << (sColors[i]) << std::endl;
    strip.setPixelColor(0, strip.Color(iColors[i][0], iColors[i][1], iColors[i][2]));
    strip.show();
    delay(rgb_delay_time);
  }
}

void setup(void) 
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("setup(): ");
  // See: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/unit/oled.pdf
  display.begin();
  canvas.setColorDepth(1); // mono color

  if (display.isEPD())
  {
    scrollstep = 16;
    display.setEpdMode(epd_mode_t::epd_fastest);
    display.invertDisplay(true);
    display.clear(TFT_BLACK);
  }
  display.setRotation(1);
  //display.setTextColor(WHITE, BLACK);
  canvas.setFont(&fonts::FreeSans9pt7b); // was: efontCN_14);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.createSprite(display.width() + 64, 72);
  
  // bool SerialEnable, bool I2CEnable, bool DisplayEnable
  // Not using I2CEnable because of fixed values for SCL and SDA in M5Atom.begin:
  NanoC6.begin(); // Init NanoC6
  
  //Serial.println(F("\n\nM5Stack Atom Matrix with RTC unit and OLED display unit test."));
  std::cout << "\n\n" << *TAG << "M5NanoC6 Timezones with OLED and RTC units test." << std::endl;
  
  //Wire.begin(SDA, SCL);

  // See: https://fcc.report/FCC-ID/2AN3WM5NANOC6/7170758.pdf page 7/8
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  pinMode(M5NANO_C6_BLUE_LED_PIN, OUTPUT);

  // Enable RGB LED Power
  pinMode(M5NANO_C6_RGB_LED_PWR_PIN, OUTPUT);
  digitalWrite(M5NANO_C6_RGB_LED_PWR_PIN, HIGH);

  strip.begin();
  strip.show();
  
  //Serial.begin(115200);  // This command is now done by M5NanoC6.begin()

  getID();

  create_maps();  // creeate zones_map

  map_replace_first_zone();

  RTC.begin();

  // setup RTC ( NTP auto setting )
  configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

  delay(1000);

  if (connect_WiFi())
  {
    /*
    * See: https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32/api-reference/system/system_time.html#sntp-time-synchronization
      See also: https://docs.espressif.com/projects/esp-idf/en/v5.0.2/esp32/api-reference/kconfig.html#config-lwip-sntp-update-delay

      CONFIG_LWIP_SNTP_UPDATE_DELAY
      This option allows you to set the time update period via SNTP. Default is 1 hour.
      Must not be below 15 seconds by specification. (SNTPv4 RFC 4330 enforces a minimum update time of 15 seconds).
      Range:
      from 15000 to 4294967295

      Default value:
      3600000
    
      See: https://github.com/espressif/esp-idf/blob/v5.0.2/components/lwip/include/apps/esp_sntp.h
      SNTP sync status
          typedef enum {
            SNTP_SYNC_STATUS_RESET,         // Reset status.
            SNTP_SYNC_STATUS_COMPLETED,     // Time is synchronized.
            SNTP_SYNC_STATUS_IN_PROGRESS,   // Smooth time sync in progress.
          } sntp_sync_status_t;
    */
    sntp_initialize();  // name sntp_init() results in compilor error "multiple definitions sntp_init()"
    //sntp_sync_status_t sntp_sync_status = sntp_get_sync_status();
    int status = sntp_get_sync_status();
    String txt = "";
    if (status == SNTP_SYNC_STATUS_RESET) // SNTP_SYNC_STATUS_RESET
      txt = "RESET";
    else if (status == SNTP_SYNC_STATUS_COMPLETED)
      txt = "COMPLETED";
    else if (status == SNTP_SYNC_STATUS_IN_PROGRESS)
      txt = "IN PROGRESS";
    else
      txt = "UNKNOWN";
    
    std::cout << *TAG << "sntp_sync_status = " << txt.c_str() << std::endl;

    zone_idx = 0;
    setTimezone(); // Set the timezone first

    /*
    if (initTime()) // Call initTime just 1 time here, then at time_sync_notification_cb()
    {
      if (set_RTC())
      {
        poll_RTC();  // Update RTCtimeinfo
        printLocalTime();
        disp_data();
      }
    }
    */
  }
  else
  {
    connect_try++;
  }
  canvas.clear();
}

void loop(void)
{
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>("loop(): ");
  unsigned long interval_t = 5 * 60 * 1000; // 5 minutes
  unsigned long curr_t = 0L;
  unsigned long elapsed_t = 0L;
  unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;
  time_t t;
  bool dummy = false;

  while (true)
  {
    dummy = ck_BtnA();

    // blink_blue_led();
    // rgb_led_wheel(false);

    if (WiFi.status() != WL_CONNECTED) // Check if we're still connected to WiFi
    {
      curr_t = millis();
      elapsed_t = curr_t - start_t;
      if (lStart || elapsed_t >= interval_t)
      {
        lStart = false;
        start_t = curr_t;
        if (connect_WiFi())
        {
          connect_try = 0;  // reset count
          if (initTime()) // was: (commented-out function) setupTime())
          {
            if (set_RTC())
            {
              printLocalTime();
              std::cout << std::endl << *TAG << "external RTC updated from NTP server datetime stamp" << std::endl;
            }
          }
        }
        else
        {
          connect_try++;
        }

        if (connect_try >= max_connect_try)
        {
          std::cout << std::endl << *TAG << "WiFi connect try failed " << (connect_try) << 
            "times.\nGoing into infinite loop....\n" << std::endl;
          break;
        }
      }
    }
    zone_chg_curr_t = millis();

    zone_chg_elapsed_t = zone_chg_curr_t - zone_chg_start_t;

    /* Do a zone change */
    if (lStart || zone_chg_elapsed_t >= zone_chg_interval_t)
    {
      if (lStart)
      {
        zone_idx = -1; // will be increased in code below
      }
      lStart = false;
      TimeToChangeZone = true;
      digitalWrite(M5NANO_C6_BLUE_LED_PIN, HIGH);  // Switch on the Blue Led
      zone_chg_start_t = zone_chg_curr_t;
      /*
        Increases the Display color index.
      */
      FSM++;
      if (FSM >= 6)
      {
        FSM = 0;
      }

      /*
      Increase the zone_index, so that the sketch
      will display data from a next timezone in the map: time_zones.
      */
      zone_idx++;
      if (zone_idx >= zone_max_idx)
      {
        zone_idx = 0;
      }
      setTimezone();
      TimeToChangeZone = false;
      if (my_debug)
        poll_RTC();
      printLocalTime();
      digitalWrite(M5NANO_C6_BLUE_LED_PIN, LOW);  // Switch off the Blue Led
      disp_data();
      // Poll NTP and set RTC to synchronize it
    }
    
    if (buttonPressed)
    {
      // We have a button press so do a software reset
      std::cout << *TAG << "Button was pressed.\n" << 
        "Going to do a software reset...\n" << std::endl;
      disp_msg("Reset..."); // there is already a wait of 6000 in disp_msg()
      esp_restart();
    }
    // printLocalTime();
    disp_data();
    NanoC6.update();  // Read the press state of the key.
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  } // end-of-while

  disp_msg("Bye...");
  std::cout << *TAG << "Bye...\n" << std::endl;
  /* Go into an endless loop after WiFi doesn't work */
  do
  {
    delay(5000);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  } while (true);
}
