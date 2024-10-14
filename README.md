M5NanoC6_Timezones.ino

This is a combination of the following earlier repos:

[repo1 ](https://github.com/PaulskPt/M5Stack_Atom_Matrix_Timezones),
[repo2](https://github.com/PaulskPt/M5Stack_M5Atom_EchoSPKR) and 
[repo3](https://github.com/PaulskPt/M5Dial_Timezones_and_beep_cmd_to_M5AtomEcho)


Hardware used:

    1) M5Stack M5NanoC6;
    2) M5Stack OLED unit;
    3) M5Stack RTC unit;
    4) M5Stack GROVE HUB;
    5) 3x GROVE 4-wire cables;
   
This project contains an Arduino sketch for a M5Stack M5NanoC6 device, a 1.3 inch OLED SH1107 display with 128 x 64 pixels and a RTC unit (8563).
 
The OLED and RTC units are connected to the GROVE Port of a M5NanoC6 device via a GROVE HUB.

About i2c_scan with following units connected to M5NanoC6, Port A: RTC unit and OLED unit:
I2C device found at address 0x3c !  = OLED unit
I2C device found at address 0x51 !  = RTC unit

After applying power to the M5NanoC6 device, the sketch will sequentially display data of six pre-programmed timezones.

For each of the six timezones, in four steps, the following data will be displayed:
   1) Time zone continent and city, for example: "Europe" and "Lisbon"; 
   2) the word "Zone" and the Timezone in letters, for example "CEST", and the offset to UTC, for example "+0100";
   3) date info, for example "Monday September 30 2024"; 
   4) time info, for example: "20:52:28 in: Lisbon".

Each time zone sequence of four displays is repeated for 25 seconds. This repeat time is defined in function ```loop()```:

```
1015 unsigned long const zone_chg_interval_t = 25 * 1000L; // 25 seconds
```
M5NanoC6 reset:

Pressing the M5NanoC6 button will cause a software reset.

On reset the Arduino Sketch will try to connect to the WiFi Access Point of your choice (set in secret.h). 
The sketch will connect to a NTP server of your choice. In this version the sketch uses a ```NTP polling system```. 
The following define sets the NTP polling interval time:

```
70 #define CONFIG_LWIP_SNTP_UPDATE_DELAY  15 * 60 * 1000 // = 15 minutes
```

At the moment of a NTP Time Synchronization, the RGB Led of the M5NanoC6 wil cycle three colors.
At each change of Timezone, the built-in Blue Led of the M5NanoC6 will blink.
The external RTC of the M5 RTC unit device will be set to the NTP datetime stamp with the local time for the current Timezone.
Next the sketch will display time zone name, timezone offset from UTC, date and time of the current Timezone.

In the M5NanoC6 sketch is pre-programmed a map (dictionary), name ```zones_map```. This map contains six timezones:

```
    zones_map[0] = std::make_tuple("Asia/Tokyo", "JST-9");
    zones_map[1] = std::make_tuple("America/Kentucky/Louisville", "EST5EDT,M3.2.0,M11.1.0");
    zones_map[2] = std::make_tuple("America/New_York", "EST5EDT,M3.2.0,M11.1.0");
    zones_map[3] = std::make_tuple("America/Sao_Paulo", "<-03>3");
    zones_map[4] = std::make_tuple("Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3");
    zones_map[5] = std::make_tuple("Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3");
```

 After reset of the M5NanoC6 the sketch will load from the file ```secret.h``` the values of ```SECRET_NTP_TIMEZONE``` and ```SECRET_NTP_TIMEZONE_CODE```, 
 and replaces the first record in the map ```zones_map``` with these values from secret.h.

M5NanoC6 Debug output:

In the sketch file of the M5NanoC6, added a global variable ```my_debug```. The majority of monitor output I made conditionally controlled by this new ```my_debug```.
See the difference in monitor output in the two monitor_output.txt files.

File secret.h:

Update the file secret.h as far as needed:
```
 a) your WiFi SSID in SECRET_SSID;
 b) your WiFi PASSWORD in SECRET_PASS;
 c) your timezone in SECRET_NTP_TIMEZONE, for example: Europe/Lisbon;
 d) your timezone code in SECRET_NTP_TIMEZONE_CODE, for example: WET0WEST,M3.5.0/1,M10.5.0;
 e) the name of the NTP server of your choice in SECRET_NTP_SERVER_1, for example: 2.pt.pool.ntp.org.
```

Update 2024-10-13: Solved a problem that there was no board "M5NanoC6". In Arduino IDE v2.3.3. BOARDS MANAGER
appeared to be installed "esp32 from Espressif Systems" version "3.1.0 RC". 
I downgraded it to the stable version "3.0.5", which contains the "M5NanoC6"
Device info in pint_arduino.h : M5Stack M5NanoC6 : USB_VID 0x303A, USB_PID 0x1001

Update 2024-10-14: in function disp_data() the integer variable disp_data_view_delay controls the "rithm" of renewal of the 4 view pages.

How this sketch works:

After startup the sketch tries to make WiFi contact. If WiFi is OK, then, at fixed intervals, polling requests will be sent to the NTP server of your choice.
The global variable CONFIG_LWIP_SNTP_UPDATE_DELAY defines the polling interval. In this moment 15 minutes.
When the datetime stamp is received from an NTP server, the external RTC will be set. Next the sketch will cycle through and
display timezone information and local date and time for each of six pre-programmed timezones.
A cycle of displaying the size timezones takes about 2 minutes and 35 seconds.

Docs:

```
M5NanoC6_Monitor_output.txt

See: Complete list of zones: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

```

Images: 

Images taken during the sketch was running are in the folder ```images```.

Links to product pages of the hardware used:

- M5Stack M5NanoC6    [info](https://shop.m5stack.com/products/m5stack-nanoc6-dev-kit);
- M5Stack OLED unit   [info](https://shop.m5stack.com/products/oled-unit-1-3-128-64-display);
- M5Stack RTC  unit   [info](https://shop.m5stack.com/products/real-time-clock-rtc-unit-hym8563);
- M5Stack GROVE HUB   [info](https://shop.m5stack.com/products/mini-hub-module);
- M5Stack GROVE Cable [info](https://shop.m5stack.com/products/4pin-buckled-grove-cable)
