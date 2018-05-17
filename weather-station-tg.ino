/**The MIT License (MIT)
  Copyright (c) 2015 by Daniel Eichhorn
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYBR_DATUM HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  See more at http://blog.squix.ch

  Adapted by Bodmer to use the faster TFT_eSPI library:
  https://github.com/Bodmer/TFT_eSPI

  Plus:
  Minor changes to text placement and auto-blanking out old text with background colour padding
  Moon phase text added
  Forecast text lines are automatically split onto two lines at a central space (some are long!)
  Time is printed with colons aligned to tidy display
  Min and max forecast temperatures spaced out
  The ` character has been changed to a degree symbol in the 36 point font
  New smart WU splash startup screen and updated progress messages
  Display does not need to be blanked between updates
  Icons nudged about slightly to add wind direction + speed
  Barometric pressure added
*/

#define SERIAL_MESSAGES

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h> // Hardware-specific library

// Additional UI functions
#include "GfxUi.h"

// Fonts created by http://oleddisplay.squix.ch/
#include "ArialRoundedMtBold_14.h"
#include "ArialRoundedMTBold_36.h"

// Download helper
#include "WebResource.h"

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// Helps with connecting to internet
#include <WiFiManager.h>

// check settings.h for adapting to your needs
#include "settings.h"
#include <JsonListener.h>
#include <WundergroundClient.h>
#include <TimeLib.h>
//#include "TimeSettings.h"
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include "SoftwareSerial.h"
#include <DFPlayer_Mini_Mp3.h>

// HOSTNAME for OTA update
#define HOSTNAME "AlarmClock"
// This is the file name used to store the calibration data
// You can change this to create new calibration files.
// The SPIFFS file name must start with "/".
#define CALIBRATION_FILE "/TouchCalData1"

// Set REPEAT_CAL to true instead of false to run calibration
// again, otherwise it will only be done once.
// Repeat calibration if you change the screen rotation.
#define REPEAT_CAL false

#define MAIN_SCREEN 0
#define ALARM_SETTINGS_SCREEN 1
#define ALARM_SCREEN 2
#define LABEL_FONT &FreeSansBold12pt7b    // Key label font 2

/*****************************
   Important: see settings.h to configure your settings!!!
 * ***************************/

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
TFT_eSPI_Button alarmSetupButton;
TFT_eSPI_Button mainScreenButton;
SoftwareSerial mySoftwareSerial(D0, D8); // RX, TX

boolean booted = true;
const int SYNC_INTERVAL = 3600; //every hour sync the time
int screen = 0;
int alarmSettings [10] = {19, 51, 0, 0, 0, 0, 0, 0, 0, 1};
GfxUi ui = GfxUi(&tft);

WebResource webResource;
const String _timeZone = "America/Detroit";
const String _apiKey = "AY0H14VE80X2";

// Set to false, if you prefere imperial/inches, Fahrenheit
WundergroundClient wunderground(false);

//declaring prototypes
void configModeCallback (WiFiManager *myWiFiManager);
void downloadCallback(String filename, int16_t bytesDownloaded, int16_t bytesTotal);
ProgressCallback _downloadCallback = downloadCallback;
void downloadResources();
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
String getMeteoconIcon(String iconText);
//void drawAstronomy();
void drawSeparator(uint16_t y);

long lastDownloadUpdate = millis();

void setup() {
#ifdef SERIAL_MESSAGES
  Serial.begin(115200);
#endif
  tft.begin();
  tft.fillScreen(TFT_BLACK);

  tft.setFreeFont(&ArialRoundedMTBold_14);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Original by: blog.squix.org", 120, 240);
  tft.drawString("Adapted by: Bodmer", 120, 260);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  SPIFFS.begin();
  //listFiles();
  //Uncomment if you want to erase SPIFFS and update all internet resources, this takes some time!
  //tft.drawString("Formatting SPIFFS, so wait!", 120, 200); SPIFFS.format();

  if (SPIFFS.exists("/WU.jpg") == true) ui.drawJpeg("/WU.jpg", 0, 10);
  if (SPIFFS.exists("/Earth.jpg") == true) ui.drawJpeg("/Earth.jpg", 0, 320 - 56); // Image is 56 pixels high
  delay(1000);
  tft.drawString("Connecting to WiFi", 120, 200);
  tft.setTextPadding(240); // Pad next drawString() text to full width to over-write old text

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);

  //or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect();

  //Manual Wifi
  //WiFi.begin(WIFI_SSID, WIFI_PWD);

  // OTA Setup
  String hostname(HOSTNAME);
  WiFi.hostname(hostname);
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();

  // download images from the net. If images already exist don't download
  tft.drawString("Downloading to SPIFFS...", 120, 200);
  tft.drawString(" ", 120, 240);  // Clear line
  tft.drawString(" ", 120, 260);  // Clear line
  downloadResources();
  //listFiles();
  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(240); // Pad next drawString() text to full width to over-write old text
  tft.drawString(" ", 120, 200);  // Clear line above using set padding width
  tft.drawString("Fetching weather data...", 120, 200);
  //delay(500);
  setSyncProvider(getTime);
  setSyncInterval(SYNC_INTERVAL);
  mySoftwareSerial.begin(9600);
  mp3_set_serial (mySoftwareSerial);
  // Delay is required before accessing player. From my experience it's ~1 sec
  mp3_set_volume(30);
  delay(500);

  // load the weather information
  updateData();
}

long lastDrew = 0;
void loop() {
  // Handle OTA update requests
  ArduinoOTA.handle();
  uint16_t t_x = 0, t_y = 0; // To store the touch coordinates
  if (screen == MAIN_SCREEN) {
    if (alarmSettings[day() + 3]) {
      if (alarmSettings[0] == hour()) {
        if (alarmSettings[1] == minute()) {
          screen = ALARM_SCREEN;
        }
      }
    }
    // Pressed will be set true is there is a valid touch on the screen
    if ( tft.getTouch(&t_x, &t_y)) {
      Serial.println("Screen touched....x:" + String(t_x) + " y:" + String(t_y));
      // tft.fillCircle(t_y, t_x, 2, TFT_BLACK);

      // / Check if any key coordinate boxes contain the touch coordinates

      if ( t_x > 0 && t_x < 80 && t_y < 280 && t_y > 80) {
        screen = ALARM_SETTINGS_SCREEN;
        delay(500);
         mp3_play();
        drawAlarmScreen();
      }
    }
    // Check if we should update the clock
    if (millis() - lastDrew > 30000 && wunderground.getSeconds() == "00") {
      drawTime();
      lastDrew = millis();
    }

    // Check if we should update weather information
    if (millis() - lastDownloadUpdate > 1000 * UPDATE_INTERVAL_SECS) {
      updateData();
      lastDownloadUpdate = millis();
    }
  } else if (screen == ALARM_SETTINGS_SCREEN) {
    if ( tft.getTouch(&t_x, &t_y)) {
      Serial.println("Screen touched....x:" + String(t_x) + " y:" + String(t_y));
      // tft.fillCircle(t_y, t_x, 2, TFT_BLACK);
      int DAY_WIDTH = 80;
      int DAY_HEIGHT = 15;
      int START_X = 205;
      int START_Y = 300;
      for (int i = 0; i < 4; i++) {
        if (t_x < START_X  && t_x > START_X - DAY_HEIGHT  && t_y < START_Y - DAY_WIDTH * i && t_y > START_Y - DAY_WIDTH * (i + 1)) {
          if (alarmSettings[i + 3]) {
            alarmSettings[i + 3] = 0;
          } else {
            alarmSettings[i + 3] = 1;
          }
          Serial.println(String(i) + " pressed");
          delay(500);
        }
      }
      DAY_WIDTH = 80;
      START_X = 190;
      START_Y = 250;
      for (int i = 0; i < 3; i++) {
        if (t_x < START_X  && t_x > START_X - DAY_HEIGHT  && t_y < START_Y - DAY_WIDTH * i && t_y > START_Y - DAY_WIDTH * (i + 1)) {
          if (alarmSettings[i + 7]) {
            alarmSettings[i + 7] = 0;
          } else {
            alarmSettings[i + 7] = 1;
          }
          Serial.println(String(i) + " pressed");
          delay(500);
        }
      }
      DAY_HEIGHT = 20;
      DAY_WIDTH = 60;
      START_X = 240;
      START_Y = 210;
      for (int i = 0; i < 3; i++) {
        if (t_x < START_X  && t_x > START_X - DAY_HEIGHT  && t_y < START_Y - DAY_WIDTH * i && t_y > START_Y - DAY_WIDTH * (i + 1)) {
          alarmSettings[i] ++;
          if (i == 0 && alarmSettings[0] > 23) {
            alarmSettings[0] = 0;
          } else if (i == 1 && alarmSettings[1] > 59) {
            alarmSettings[1] = 0;
          }
          Serial.println(String(i) + " pressed");
          delay(500);
        }
      }
      drawAlarmScreen();
      if ( t_x > 0 && t_x < 80 && t_y < 280 && t_y > 80) {
        screen = MAIN_SCREEN;
        tft.fillScreen(TFT_BLACK);
        delay(500);
        updateData();
      }
    }

  } else if (screen == ALARM_SCREEN) {
    if (alarmSettings[day() + 3]) {
      if (alarmSettings[0] == hour()) {
        if (alarmSettings[1] == minute()) {
          tft.fillScreen(TFT_RED);
          delay(500);
          tft.fillScreen(TFT_GREEN);
        } else {
          screen = MAIN_SCREEN;
          tft.fillScreen(TFT_BLACK);
          updateData();
        }
      }
    }

  }
}

// Called if WiFi has not been configured yet
void configModeCallback (WiFiManager *myWiFiManager) {
  tft.setTextDatum(BC_DATUM);
  tft.setFreeFont(&ArialRoundedMTBold_14);
  tft.setTextColor(TFT_ORANGE);
  tft.drawString("Wifi Manager", 120, 28);
  tft.drawString("Please connect to AP", 120, 42);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(myWiFiManager->getConfigPortalSSID(), 120, 56);
  tft.setTextColor(TFT_ORANGE);
  tft.drawString("To setup Wifi Configuration", 120, 70);
}

// callback called during download of files. Updates progress bar
void downloadCallback(String filename, int16_t bytesDownloaded, int16_t bytesTotal) {
  Serial.println(String(bytesDownloaded) + " / " + String(bytesTotal));

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(240);

  int percentage = 100 * bytesDownloaded / bytesTotal;
  if (percentage == 0) {
    tft.drawString(filename, 120, 220);
  }
  if (percentage % 5 == 0) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.textWidth(" 888% "));
    tft.drawString(String(percentage) + "%", 120, 245);
    ui.drawProgressBar(10, 225, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);
  }

}



// draws the clock
void drawTime() {

  tft.setFreeFont(&ArialRoundedMTBold_36);
  char buff[5];
  int hr_24, hr_12;
  char* AM_PM = "AM";
  hr_24 = hour();

  if (hr_24 == 0) {
    hr_12 = 12;
  }
  else {
    hr_12 = hr_24 % 12;
  };
  if (hr_24 > 12 ) {
    AM_PM = "PM";
  }
  sprintf(buff, "%2d:%02d %2s", hr_12, minute(), AM_PM);

  String timeNow = String(buff);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 44:44 "));  // String width + margin
  tft.drawString(timeNow, 120, 53);

  tft.setFreeFont(&ArialRoundedMTBold_14);
  char buff2[32];
  char buff3[8];
  sprintf(buff3, "%s, ", dayStr(weekday()));
  sprintf(buff2, "%s %2d %4d", monthStr(month()), day(), year());
  String date = String(buff3) + String(buff2);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Ddd, 44 Mmm 4444 "));  // String width + margin
  tft.drawString(date, 120, 16);

  drawSeparator(54);

  tft.setTextPadding(0);
}

// draws current weather information
void drawCurrentWeather() {
  // Weather Icon
  String weatherIcon = getMeteoconIcon(wunderground.getTodayIcon());
  //uint32_t dt = millis();
  ui.drawBmp(weatherIcon + ".bmp", 0, 59);
  //Serial.print("Icon draw time = "); Serial.println(millis()-dt);

  // Weather Text

  String weatherText = wunderground.getWeatherText();
  //weatherText = "Heavy Thunderstorms with Small Hail"; // Test line splitting with longest(?) string

  tft.setFreeFont(&ArialRoundedMTBold_14);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  int splitPoint = 0;
  int xpos = 230;
  splitPoint =  splitIndex(weatherText);
  if (splitPoint > 16) xpos = 235;

  tft.setTextPadding(tft.textWidth("Heavy Thunderstorms"));  // Max anticipated string width
  if (splitPoint) tft.drawString(weatherText.substring(0, splitPoint), xpos, 72);
  tft.setTextPadding(tft.textWidth(" with Small Hail"));  // Max anticipated string width + margin
  tft.drawString(weatherText.substring(splitPoint), xpos, 87);

  tft.setFreeFont(&ArialRoundedMTBold_36);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  // Font ASCII code 96 (0x60) modified to make "`" a degree symbol
  tft.setTextPadding(tft.textWidth("-88`")); // Max width of vales

  weatherText = wunderground.getCurrentTemp();
  if (weatherText.indexOf(".")) weatherText = weatherText.substring(0, weatherText.indexOf(".")); // Make it integer temperature
  if (weatherText == "") weatherText = "?";  // Handle null return
  tft.drawString(weatherText + "`", 221, 100);

  tft.setFreeFont(&ArialRoundedMTBold_14);

  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  if (IS_METRIC) tft.drawString("C ", 221, 100);
  else  tft.drawString("F ", 221, 100);

  //tft.drawString(wunderground.getPressure(), 180, 30);

  weatherText = ""; //wunderground.getWindDir() + " ";
  weatherText += String((int)(wunderground.getWindSpeed().toInt() * WIND_SPEED_SCALING)) + WIND_SPEED_UNITS;

  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(tft.textWidth(" 888 mph")); // Max string length?
  tft.drawString(weatherText, 128, 136);

  weatherText = wunderground.getPressure();

  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(tft.textWidth(" 8888mb")); // Max string length?
  tft.drawString(weatherText, 230, 136);

  weatherText = wunderground.getWindDir();

  int windAngle = 0;
  String compassCardinal = "";
  switch (weatherText.length()) {
    case 1:
      compassCardinal = "N E S W "; // Not used, see default below
      windAngle = 90 * compassCardinal.indexOf(weatherText) / 2;
      break;
    case 2:
      compassCardinal = "NE SE SW NW";
      windAngle = 45 + 90 * compassCardinal.indexOf(weatherText) / 3;
      break;
    case 3:
      compassCardinal = "NNE ENE ESE SSE SSW WSW WNW NNW";
      windAngle = 22 + 45 * compassCardinal.indexOf(weatherText) / 4; // 22 should be 22.5 but accuracy is not needed!
      break;
    default:
      if (weatherText == "Variable") windAngle = -1;
      else {
        //                 v23456v23456v23456v23456 character ruler
        compassCardinal = "North East  South West"; // Possible strings
        windAngle = 90 * compassCardinal.indexOf(weatherText) / 6;
      }
      break;
  }

  tft.fillCircle(128, 110, 23, TFT_BLACK); // Erase old plot, radius + 1 to delete stray pixels
  tft.drawCircle(128, 110, 22, TFT_DARKGREY);    // Outer ring - optional
  if ( windAngle >= 0 ) fillSegment(128, 110, windAngle - 15, 30, 22, TFT_GREEN); // Might replace this with a bigger rotating arrow
  tft.drawCircle(128, 110, 6, TFT_RED);

  drawSeparator(153);

  tft.setTextDatum(TL_DATUM); // Reset datum to normal
  tft.setTextPadding(0); // Reset padding width to none
}

// draws the three forecast columns
void drawForecast() {
  drawForecastDetail(10, 171, 0);
  drawForecastDetail(95, 171, 2);
  drawForecastDetail(180, 171, 4);
  drawSeparator(171 + 69);
}

// helper for the forecast columns
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
  tft.setFreeFont(&ArialRoundedMTBold_14);

  String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  day.toUpperCase();

  tft.setTextDatum(BC_DATUM);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("WWW"));
  tft.drawString(day, x + 25, y);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("-88   -88"));
  tft.drawString(wunderground.getForecastHighTemp(dayIndex) + "   " + wunderground.getForecastLowTemp(dayIndex), x + 25, y + 14);

  String weatherIcon = getMeteoconIcon(wunderground.getForecastIcon(dayIndex));
  ui.drawBmp("/mini/" + weatherIcon + ".bmp", x, y + 15);

  tft.setTextPadding(0); // Reset padding width to none
}

void drawalarmSetupButton() {
  tft.setFreeFont(LABEL_FONT);
  alarmSetupButton.initButton(&tft, 120, 290, 120, 60, TFT_WHITE, TFT_BLUE,
                              TFT_WHITE, "ALARM", 1);
  alarmSetupButton.drawButton();
}

void drawAlarmScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(LABEL_FONT);
  mainScreenButton.initButton(&tft, 120, 290, 120, 60, TFT_WHITE, TFT_BLUE,
                              TFT_WHITE, "BACK", 1);
  mainScreenButton.drawButton();
  tft.setFreeFont(&ArialRoundedMTBold_36);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 44:44 "));  // String width + margin
  char buff[5];
  int hr_24, hr_12;
  char* AM_PM = "AM";
  hr_24 = alarmSettings[0];

  if (hr_24 == 0) {
    hr_12 = 12;
  }
  else {
    hr_12 = hr_24 % 12;
  };
  if (alarmSettings[0] > 12) {
    AM_PM = "PM";
  }
  sprintf(buff, "%2d:%02i %2s", hr_12, alarmSettings[1], AM_PM);

  String timeNow = String(buff);
  tft.drawString(timeNow, 120, 53);
  drawSeparator(130);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextPadding(tft.textWidth(" Mm "));  // String width + margin
  drawWeekday("Su", 30, 80, alarmSettings[3]);
  drawWeekday("Mo", 90, 80, alarmSettings[4]);
  drawWeekday("Tu", 150, 80, alarmSettings[5]);
  drawWeekday("We", 210, 80, alarmSettings[6]);
  drawWeekday("Th", 60, 110, alarmSettings[7]);
  drawWeekday("Fr", 120, 110, alarmSettings[8]);
  drawWeekday("Sa", 180, 110, alarmSettings[9]);
}

void drawWeekday(String day, int x, int y, int setting) {
  if (setting) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  }
  tft.drawString(day, x, y);
}

// Helper function, should be part of the weather station library and should disappear soon
String getMeteoconIcon(String iconText) {
  if (iconText == "F") return "chanceflurries";
  if (iconText == "Q") return "chancerain";
  if (iconText == "W") return "chancesleet";
  if (iconText == "V") return "chancesnow";
  if (iconText == "S") return "chancetstorms";
  if (iconText == "B") return "clear";
  if (iconText == "Y") return "cloudy";
  if (iconText == "F") return "flurries";
  if (iconText == "M") return "fog";
  if (iconText == "E") return "hazy";
  if (iconText == "Y") return "mostlycloudy";
  if (iconText == "H") return "mostlysunny";
  if (iconText == "H") return "partlycloudy";
  if (iconText == "J") return "partlysunny";
  if (iconText == "W") return "sleet";
  if (iconText == "R") return "rain";
  if (iconText == "W") return "snow";
  if (iconText == "B") return "sunny";
  if (iconText == "0") return "tstorms";


  return "unknown";
}

// if you want separators, uncomment the tft-line
void drawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
}

// determine the "space" split point in a long string
int splitIndex(String text)
{
  int index = 0;
  while ( (text.indexOf(' ', index) >= 0) && ( index <= text.length() / 2 ) ) {
    index = text.indexOf(' ', index) + 1;
  }
  if (index) index--;
  return index;
}

// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically right align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int rightOffset(String text, String sub)
{
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(index));
}

// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically left align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int leftOffset(String text, String sub)
{
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(0, index));
}

// Draw a segment of a circle, centred on x,y with defined start_angle and subtended sub_angle
// Angles are defined in a clockwise direction with 0 at top
// Segment has radius r and it is plotted in defined colour
// Can be used for pie charts etc, in this sketch it is used for wind direction
#define DEG2RAD 0.0174532925 // Degrees to Radians conversion factor
#define INC 2 // Minimum segment subtended angle and plotting angle increment (in degrees)
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour)
{
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw colour blocks every INC degrees
  for (int i = start_angle; i < start_angle + sub_angle; i += INC) {

    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;

    tft.fillTriangle(x1, y1, x2, y2, x, y, colour);

    // Copy segment end to sgement start for next segment
    x1 = x2;
    y1 = y2;
  }
}

time_t getTime() {
  WiFiClient client;
  if (!client.connect("api.timezonedb.com", 80)) {
    return 0;
  }
  //  String URL = "GET /v2/get-time-zone?key=" + _timeZone + AY0H14VE80X2&format=json&by=zone&zone=" + _apiKey
  client.print("GET /v2/get-time-zone?key=" + String(_apiKey) + "&format=json&by=zone&zone=");
  client.print(_timeZone);
  client.print(" HTTP/1.1\r\n");
  client.print("Host: api.timezonedb.com\r\n");
  client.print("Connection: close\r\n");
  client.println();
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      client.stop();
      return 0;
    }
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    return 0;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    return 0;
  }

  //skip extra characters
  if (client.available()) {
    client.readStringUntil('\r');
  }

  // Allocate JsonBuffer
  // Use arduinojson.org/assistant to compute the capacity.
  DynamicJsonBuffer jsonBuffer;
  // Parse JSON object
  JsonObject& root = jsonBuffer.parseObject(client);
  if (!root.success()) {
    return 0;
  }
  return root["timestamp"].as<long>();

}

void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!SPIFFS.begin()) {
    Serial.println("Formating file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      fs::File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char *)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL) {
    // calibration data valid
    tft.setTouch(calData);
  } else {
    // data not valid so recalibrate
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // store data
    fs::File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}
// Download the bitmaps
void downloadResources() {
  // tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&ArialRoundedMTBold_14);
  char id[5];

  // Download WU graphic jpeg first and display it, then the Earth view
  webResource.downloadFile((String)"http://i.imgur.com/njl1pMj.jpg", (String)"/WU.jpg", _downloadCallback);
  if (SPIFFS.exists("/WU.jpg") == true) ui.drawJpeg("/WU.jpg", 0, 10);

  webResource.downloadFile((String)"http://i.imgur.com/v4eTLCC.jpg", (String)"/Earth.jpg", _downloadCallback);
  if (SPIFFS.exists("/Earth.jpg") == true) ui.drawJpeg("/Earth.jpg", 0, 320 - 56);

  //webResource.downloadFile((String)"http://i.imgur.com/IY57GSv.jpg", (String)"/Horizon.jpg", _downloadCallback);
  //if (SPIFFS.exists("/Horizon.jpg") == true) ui.drawJpeg("/Horizon.jpg", 0, 320-160);

  //webResource.downloadFile((String)"http://i.imgur.com/jZptbtY.jpg", (String)"/Rainbow.jpg", _downloadCallback);
  //if (SPIFFS.exists("/Rainbow.jpg") == true) ui.drawJpeg("/Rainbow.jpg", 0, 0);

  for (int i = 0; i < 19; i++) {
    sprintf(id, "%02d", i);
    webResource.downloadFile("http://www.squix.org/blog/wunderground/" + wundergroundIcons[i] + ".bmp", wundergroundIcons[i] + ".bmp", _downloadCallback);
  }
  for (int i = 0; i < 19; i++) {
    sprintf(id, "%02d", i);
    webResource.downloadFile("http://www.squix.org/blog/wunderground/mini/" + wundergroundIcons[i] + ".bmp", "/mini/" + wundergroundIcons[i] + ".bmp", _downloadCallback);
  }
  /* for (int i = 0; i < 24; i++) {
     webResource.downloadFile("http://www.squix.org/blog/moonphase_L" + String(i) + ".bmp", "/moon" + String(i) + ".bmp", _downloadCallback);
    }*/
}

// Update the internet based information and update screen
void updateData() {
  // booted = true;  // Test only
  // booted = false; // Test only

  if (booted) ui.drawJpeg("/WU.jpg", 0, 10); // May have already drawn this but it does not take long
  else tft.drawCircle(22, 22, 18, TFT_DARKGREY); // Outer ring - optional

  if (booted) drawProgress(20, "Updating time...");
  else fillSegment(22, 22, 0, (int) (20 * 3.6), 16, TFT_NAVY);

  //timeClient.updateTime();
  if (booted) drawProgress(50, "Updating conditions...");
  else fillSegment(22, 22, 0, (int) (50 * 3.6), 16, TFT_NAVY);

  wunderground.updateConditions(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  if (booted) drawProgress(70, "Updating forecasts...");
  else fillSegment(22, 22, 0, (int) (70 * 3.6), 16, TFT_NAVY);

  wunderground.updateForecast(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  if (booted) drawProgress(90, "Updating astronomy...");
  else fillSegment(22, 22, 0, (int) (90 * 3.6), 16, TFT_NAVY);

  //wunderground.updateAstronomy(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  // lastUpdate = timeClient.getFormattedTime();
  // readyForWeatherUpdate = false;
  if (booted) drawProgress(100, "Done...");
  else fillSegment(22, 22, 0, 360, 16, TFT_NAVY);

  if (booted) delay(2000);

  if (booted) tft.fillScreen(TFT_BLACK);
  else   fillSegment(22, 22, 0, 360, 22, TFT_BLACK);

  //tft.fillScreen(TFT_CYAN); // For text padding and update graphics over-write checking only
  drawTime();
  drawCurrentWeather();
  drawForecast();
  //drawAstronomy();
  drawalarmSetupButton();
  booted = false;
}

// Progress bar helper
void drawProgress(uint8_t percentage, String text) {
  tft.setFreeFont(&ArialRoundedMTBold_14);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(240);
  tft.drawString(text, 120, 220);

  ui.drawProgressBar(10, 225, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);

  tft.setTextPadding(0);
}
