/* 
*  Test sketch for M5Stack M5NanoC6 device with a 1.3 inch OLED SH1107 display with 128 x 64 pixels and a RTC unit (8563)
* by @PaulskPt 2024-10-13
* The OLED and RTC units are connected to the GROVE Port of a M5NanoC6 device via a GROVE HUB.
* Update: 2024-10-12. Ported this sketch from  a M5Stack M5Dial device.
*
* About i2c_scan with following units connected to M5NanoC6, Port A: RTC unit and OLED unit:
* I2C device found at address 0x3c !  = OLED unit
* I2C device found at address 0x51 !  = RTC unit
*
* Update 2024-10-13: Solved a problem that there was no board "M5NanoC6". In Arduino IDE v2.3.3. BOARDS MANAGER
*     appeared to be installed "esp32 from Espressif Systems" version "3.1.0 RC". 
*     I downgraded it to the stable version "3.0.5", which contains the "M5NanoC6"
* Device info in pint_arduino.h : M5Stack M5NanoC6 : USB_VID 0x303A, USB_PID 0x1001
*
* Update 2024-10-14: in function disp_data() the integer variable disp_data_view_delay controls the "rithm" of renewal of the 4 view pages.
*
* How this sketch works:
* After startup the sketch the function create_maps() reads all the timezone and timezone_code text strings that are defined
* in the file secret.h into a map (like a dictionary in Python).
* Then the sketch tries to make WiFi contact. If WiFi is OK, then s polling sequence interval request will be set.
* At the polling interval time a requeste for a datetime will be sent to the NTP server.
* The global variable CONFIG_LWIP_SNTP_UPDATE_DELAY defines the polling interval. In this moment 15 minutes.
* When the datetime stamp is received from an SNTP server, the external RTC will be set. Next the sketch will cycle through and
* display timezone information and local date and time for each of seven pre-programmed timezones.
* A cycle of displaying the seven timezones takes about 3 minutes.
*
* Update 2024-10-18: 
* Added preprocessing directive DEBUG_OUTPUT. It is set to zero. Handles a block in function time_sync_notification_cb()
* Added global variable time_sync.
* changed global variable CONFIG_LWIP_SNTP_UPDATE_DELAY into uint32_t. 
* Added global variables: CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS, CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_MINUTES.
* function time_sync_notification_cb() revised (with help of MS CoPilot).
* changed function bool().
*
* See: Complete list of zones: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
*
* License: MIT.
*/
#include <M5GFX.h>
#include <M5UnitOLED.h>
#include <M5NanoC6.h>
#include <Unit_RTC.h>

//#include <esp_sntp.h>
#ifdef sntp_getoperatingmode
#undef sntp_getoperatingmode
#endif

#include <esp_sntp.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <stdlib.h>   // for putenv
#include <time.h>
#include <DateTime.h> // See: /Arduino/libraries/ESPDateTime/src

//#include <M5Unified.h>
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

// namespace {

#define DEBUG_OUTPUT 0 // Off

/*
* IMPORTANT NOTE:
* Don't use expressions like 
* const uint32_t CONFIG_LWIP_SNTP_UPDATE_DELAY = 300000U; 
* It provokes a compilor error. The compiler "ate" the #define versions below
*/

// Interval set to: 900000 mSec = 15 minutes in milliseconds (15 seconds is the minimum),
// See: https://github.com/espressif/esp-idf/blob/master/components/lwip/apps/sntp/sntp.c
// 300000 dec = 0x493E0 = 20 bits  ( 5 minutes)
// 900000 dec = 0xDBBA0 = 20 bits  (15 minutes)

// 15U * 60U * 1000U = 15 minutes in milliseconds
#define CONFIG_LWIP_SNTP_UPDATE_DELAY (15 * 60 * 1000)  // 15 minutes
#define CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS   CONFIG_LWIP_SNTP_UPDATE_DELAY / 1000
#define CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_MINUTES   CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS / 60

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
#define WIFI_MOBILE_SSID SECRET_MOBILE_SSID
#define WIFI_MOBILE_PASSWORD SECRET_MOBILE_PASS
#define NTP_SERVER1   SECRET_NTP_SERVER_1 // "0.pool.ntp.org"
#define NTP_SERVER2   "1.pool.ntp.org"
#define NTP_SERVER3   "2.pool.ntp.org"

std::string elem_zone;
std::string elem_zone_code;
std::string elem_zone_code_old;
bool zone_has_changed = false;

bool my_debug = false;
bool lStart = true;
bool sync_time = false;
time_t time_sync_epoch_at_start = 0;
time_t last_time_sync_epoch = 0; // see: time_sync_notification_cb()
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

/* See: https://github.com/espressif/arduino-esp32/blob/master/variants/m5stack_nanoc6/pins_arduino.h */
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
static constexpr const char* wd[7] PROGMEM = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
char text[50];
size_t textlen = 0;

unsigned long zone_chg_start_t = millis();
bool TimeToChangeZone = false;

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

/* Code by MS CoPilot */
// The sntp callback function
void time_sync_notification_cb(struct timeval *tv) {
  static constexpr const char txt1[] PROGMEM = "time_sync_notification_cb(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);

  // Get the current time  (very important!)
  time_t currentTime = time(nullptr);
  // Convert time_t to GMT struct tm
  struct tm* gmtTime = gmtime(&currentTime);
  uint16_t diff_t;

#if DEBUG_OUTPUT
  // Added by @PaulskPt. I want to see the state of global var lStart!
  static constexpr const char txt2[] PROGMEM = "lStart = ";
  std::cout << *TAG << txt2 << lStart << std::endl;

  static constexpr const char txt3[] PROGMEM = "Current time_t: ";
  std::cout << *TAG << txt3 << currentTime << std::endl;
#endif
  // Added by @PaulskPt. I want to see GMT time in this moment!
  static constexpr const char txt4[] PROGMEM = "Current GMT Time: ";
  std::cout << *TAG << txt4 << asctime(gmtTime);
  // ... end of addition by @PaulskPt

  // Set the starting epoch time if not set, only when lStart is true
  if (lStart && (time_sync_epoch_at_start == 0) && (currentTime > 0)) {
    time_sync_epoch_at_start = currentTime;  // Set only once!
  }

  // Set the last sync epoch time if not set
  if ((last_time_sync_epoch == 0) && (currentTime > 0)) {
    last_time_sync_epoch = currentTime;
  }

  if (currentTime > 0) {   
    // code added by @PaulskPt to test my ideas
    diff_t = currentTime - last_time_sync_epoch;
    last_time_sync_epoch = currentTime;
      
#if DEBUG_OUTPUT
    static constexpr const char txt5[]  PROGMEM = "CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_";
    static constexpr const char txt5a[] PROGMEM = "SECONDS = ";
    static constexpr const char txt5b[] PROGMEM = "MINUTES = ";
    std::cout << txt5 << txt5a << std::to_string(CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS) << std::endl;
    std::cout << txt5 << txt5b << std::to_string(CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_MINUTES) << std::endl;
    static constexpr const char txt6[] PROGMEM = "currentTime";
    static constexpr const char txt7[] PROGMEM = "last_time_sync_epoch";
    static constexpr const char txt8[] PROGMEM = "diff_t";
    std::cout << *TAG << txt6 << " = " << currentTime << ", " << txt7 << " = " << last_time_sync_epoch << std::endl;
    std::cout << *TAG << txt8 << " = " << diff_t << std::endl;
#endif

    if ((diff_t >= CONFIG_LWIP_SNTP_UPDATE_DELAY_IN_SECONDS) || lStart) {
      sync_time = true; // See loop initTime
      ntp_sync_notification_txt(true);
    }
    // end of code added by @PaulskPt
  }
}

/* ... End of code by MS CoPilot */

void esp_sntp_initialize() {
  static constexpr const char txt1[] PROGMEM = "sntp_initialize(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
   if (esp_sntp_enabled()) { 
    esp_sntp_stop();  // prevent initialization error
  }
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, NTP_SERVER1);
  esp_sntp_set_sync_interval(CONFIG_LWIP_SNTP_UPDATE_DELAY);
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb); // Set the notification callback function
  esp_sntp_init();

  // check the set sync_interval
  uint32_t rcvd_sync_interval_secs = esp_sntp_get_sync_interval();
  static constexpr const char txt4[] PROGMEM = "sntp polling interval (readback from SNTP server): ";
  static constexpr const char txt5[] PROGMEM = " Minute(s)";
  std::cout << *TAG << txt4 << std::to_string(rcvd_sync_interval_secs/60000) << txt5 << std::endl;
}

void setTimezone(void)
{
  static constexpr const char txt1[] PROGMEM = "setTimezone(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  elem_zone = std::get<0>(zones_map[zone_idx]);
  elem_zone_code = std::get<1>(zones_map[zone_idx]);
  if (elem_zone_code != elem_zone_code_old)
  {
    elem_zone_code_old = elem_zone_code;
    const char s1[] PROGMEM = "has changed to: ";
    zone_has_changed = true;
  }
  setenv("TZ",elem_zone_code.c_str(),1);
  delay(500);
  tzset();
  delay(500);
}

bool initTime(void)
{
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  bool ret = false;
  static constexpr const char txt1[] PROGMEM = "initTime(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  elem_zone = std::get<0>(zones_map[zone_idx]);

#ifndef ESP32
#define ESP32 (1)
#endif

setTimezone();
std::string my_tz_code = getenv("TZ");

// See: /Arduino/libraries/ESPDateTime/src/DateTime.cpp, lines 76-80
#if defined(ESP8266)
  configTzTime(elem_zone_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3); 
#elif defined(ESP32)
  //static constexpr const char txt7[] PROGMEM = "Setting configTzTime to: \"";
  //std::cout << *TAG << txt7 << my_tz_code.c_str() << "\"" << std::endl;
  configTzTime(my_tz_code.c_str(), NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);  // This one is use for the M5Stack Atom Matrix
#endif

  while (!getLocalTime(&timeinfo, 1000))
  {
    std::cout << "." << std::flush;
    delay(1000);
  };
  static constexpr const char txt8[] PROGMEM = "NTP Connected. ";
  std::cout << *TAG << txt8 << std::endl;

  if (timeinfo.tm_sec == 0 && timeinfo.tm_min  == 0 && timeinfo.tm_hour  == 0 &&
      timeinfo.tm_mday == 0 && timeinfo.tm_mon  == 0 && timeinfo.tm_year  == 0 &&
      timeinfo.tm_wday == 0 && timeinfo.tm_yday == 0 && timeinfo.tm_isdst == 0)
  {
    static constexpr const char txt9[] PROGMEM = "Failed to obtain datetime from NTP";
    std::cout << *TAG << txt9 << std::endl;
  }
  else
  {
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
  static constexpr const char txt1[] PROGMEM = "Setting time: ";
  std::cout << txt1 << (asctime(&tm)) << std::endl;
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
}

bool set_RTC(void)
{
  bool ret = false;
  // static constexpr const char txt1[] PROGMEM = "\nset_RTC(): external RTC ";
  // std::cout << txt1 << "timeinfo.tm_year = " << (timeinfo.tm_year) << std::endl;
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
    static constexpr const char txt2[] PROGMEM = "set_RTC(): external RTC has been set";
    std::cout << txt2 << std::endl;

  }
  return ret;
}

void printLocalTime()
{ 
  // Serial.print("printLocalTimer(): timeinfo.tm_year = ");
  // Serial.println(timeinfo.tm_year);
  static constexpr const char txt1[] PROGMEM = "printLocalTime(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  struct tm my_timeinfo;
  if (getLocalTime(&my_timeinfo)) // update local time
  {
    if (my_timeinfo.tm_year + 1900 > 1900)
    {
      static constexpr const char txt3[] PROGMEM = "Timezone: ";
      static constexpr const char txt4[] PROGMEM = ", datetime: ";
      elem_zone  = std::get<0>(zones_map[zone_idx]);
      std::cout << *TAG << txt3 << elem_zone.c_str() << txt4 << std::put_time(&my_timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ") << std::endl;

    }
  }
}

void disp_data(void)
{
  static constexpr const char txt1[] PROGMEM = "disp_data(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  canvas.clear();
  cursor_x = canvas.getCursorX() - scrollstep;
  if (cursor_x <= 0)
  {
    textpos  = 0;
    cursor_x = display.width();
  }

  if (!getLocalTime(&timeinfo)) // update local time
  {
    static constexpr const char txt2[] PROGMEM = "failed to get local time";
    std::cout << *TAG << txt2 << std::endl;
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
  static constexpr const char txt1[] PROGMEM = "connect_WiFi: ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  bool ret = false;

  /* First try connect to Samsung mobile phone */
  static constexpr const char txt2[] PROGMEM = "trying to connect WiFi to mobile phone ";
  std::cout << txt2 << std::endl;
  WiFi.begin(WIFI_MOBILE_SSID, WIFI_MOBILE_PASSWORD);
  for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
  {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
    ret = true;
    static constexpr const char txt3[] PROGMEM = "\r\nWiFi Connected to mobile phone";
    static constexpr const char txt3a[] PROGMEM = "WiFi mobile OK";
    std::cout << txt3 << std::endl;
    disp_msg(txt3a, 3000);
  }
  else
  {
    /* Try WiFi fixed (at home) */
    static constexpr const char txt4[] PROGMEM = "trying to connect WiFi to fixed AP";
    WiFi.begin( WIFI_SSID, WIFI_PASSWORD );
    for (int i = 20; i && WiFi.status() != WL_CONNECTED; --i)
    {
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) 
    {
      ret = true;
      static constexpr const char txt5[] PROGMEM = "\r\nWiFi Connected to fixed AP";
      static constexpr const char txt4a[] PROGMEM = "WiFi fixed OK";
      std::cout << txt5 << std::endl;
      disp_msg(txt4a, 3000);

    }
    else
    {
      static constexpr const char txt6[] PROGMEM = "WiFi connection failed.";
      std::cout << "\r\n" << txt6 << std::endl;
    }
  }
  return ret;
}

bool ck_BtnA(void)
{
  NanoC6.update();
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
  static constexpr const char txt[] PROGMEM = "RGB Led set to: ";
  static constexpr const char sColors[4][6] PROGMEM = {"RED", "GREEN", "BLUE", "BLACK"};
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
    if (pr_txt)
      std::cout << (txt) << (sColors[i]) << std::endl;
    strip.setPixelColor(0, strip.Color(iColors[i][0], iColors[i][1], iColors[i][2]));
    strip.show();
    delay(rgb_delay_time);
  }
}

void setup(void) 
{
  static constexpr const char txt1[] PROGMEM = "setup(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
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
  
  NanoC6.begin();
  
  //Serial.println(F("\n\nM5Stack Atom Matrix with RTC unit and OLED display unit test."));
  static constexpr const char txt2[] PROGMEM = "M5NanoC6 Timezones with OLED and RTC units test.";
  disp_msg("Timezones...", 3000);
  std::cout << "\n\n" << *TAG << txt2 << std::endl;
  
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

  create_maps();  // creeate zones_map

  RTC.begin();

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
    esp_sntp_initialize();  // name sntp_init() results in compilor error "multiple definitions sntp_init()"
    //sntp_sync_status_t sntp_sync_status = sntp_get_sync_status();
    int status = esp_sntp_get_sync_status();
    static constexpr const char txt6[] PROGMEM = "sntp sync status = ";
    std::cout << *TAG << txt6 << std::to_string(status) << std::endl;

    //zone_idx = 0;
    //setTimezone(); // Set the timezone first
  }
  else
  {
    connect_try++;
  }
  canvas.clear();
}

void loop(void)
{
  static constexpr const char txt1[] PROGMEM = "loop(): ";
  std::shared_ptr<std::string> TAG = std::make_shared<std::string>(txt1);
  unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
  unsigned long zone_chg_curr_t = 0L;
  unsigned long zone_chg_elapsed_t = 0L;
  time_t t;
  bool dummy = false;

  while (true)
  {
    dummy = ck_BtnA();

    if (WiFi.status() != WL_CONNECTED) // Check if we're still connected to WiFi
    {
      if (connect_WiFi())
        connect_try = 0;  // reset count
      else
        connect_try++;

      if (connect_try >= max_connect_try)
      {
        static constexpr const char txt2[] PROGMEM = "WiFi connect try failed ";
        static constexpr const char txt3[] PROGMEM = "times.\nGoing into infinite loop....\n";
        std::cout << std::endl << *TAG << txt2 << (connect_try) << txt3 << std::endl;
        break;
      }
    }

    if (sync_time || lStart) {
      if (sync_time) {
        if (initTime()) {
          time_t t = time(NULL);
          static constexpr const char txt4[] PROGMEM = "time synchronized at time (UTC): ";
          std::cout << *TAG << txt4 << asctime(gmtime(&t)) << std::flush;  // prevent a 2nd LF. Do not use std::endl

          if (set_RTC()) {
            static constexpr const char txt5[] PROGMEM = "external RTC updated from NTP server datetime stamp";
            std::cout << *TAG << txt5 << std::endl;
          }
        }
        sync_time = false;
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
      
      if (zone_idx == 0)
        std::cout << std::endl; // blank line
      
      static constexpr const char txt6[] PROGMEM = "new zone_idx = ";
      std::cout << *TAG << txt6 << zone_idx << std::endl;
      setTimezone();
      TimeToChangeZone = false;
      printLocalTime();
      digitalWrite(M5NANO_C6_BLUE_LED_PIN, LOW);  // Switch off the Blue Led
      disp_data();
      // Poll NTP and set RTC to synchronize it
    }
    
    if (buttonPressed)
    {
      // We have a button press so do a software reset
      static constexpr const char txt7[] PROGMEM = "Button was pressed.\n";
      static constexpr const char txt8[] PROGMEM = "Going to do a software reset...\n";
      static constexpr const char txt9[] PROGMEM = "Reset...";
      std::cout << *TAG << txt7 << 
        txt8 << std::endl;
      disp_msg(txt9); // there is already a wait of 6000 in disp_msg()
      esp_restart();
    }
    // printLocalTime();
    disp_data();
    lStart = false;
    NanoC6.update();  // Read the press state of the key.
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  } // end-of-while

  static constexpr const char txt10[] PROGMEM = "Bye...";
  disp_msg(txt10);
  std::cout << *TAG << txt10 << std::endl << std::endl;
  /* Go into an endless loop after WiFi doesn't work */
  do
  {
    delay(5000);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  } while (true);
}
