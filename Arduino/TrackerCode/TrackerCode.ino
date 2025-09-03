#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <HTTPClient.h>
#include "WiFi.h"
#include <esp_wifi.h>
#include <TimeLib.h>

// pins for serial connection to gps
static const int RXPin = D7, TXPin = D6;
static const uint32_t GPSBaud = 9600;

// The TinyGPSPlus object
RTC_DATA_ATTR TinyGPSPlus gps;


// The serial connection to the GNSS module
RTC_DATA_ATTR SoftwareSerial ss(RXPin, TXPin);

// Number in meteres, how far to make new points/consider being at home
const int DISTANCE_UPDATE = 60; 
const int HOME_SIZE = 60;

// How often to run main loop
RTC_DATA_ATTR unsigned long long SECS_BETWEEN = 10;
const unsigned long long MIN_SECS_BETWEEN = 10;
const unsigned long long MAX_SECS_BETWEEN = 200;



/*
States:
SETUP -- before we get into the main loop, grabs 1 location ping to see if we are at home, if at home HOME else TRACK.
TRACK -- grabs location and logs it, if at home HOME_UPLOAD else remain
HOME_UPLOAD -- starts up wifi and clears and sends all logged points, shuts off wifi and switch to HOME
HOME --    if leaves home area switch to TRACK else stay
*/

enum States {
    STATE_SETUP,
    STATE_TRACK,
    STATE_HOME_UPLOAD,
    STATE_HOME,
    STATE_ERROR,
};

// On the ESP-32S3 we have in theory we 16KB of RTC Slow Memory. This memory survives deep sleep
// 1KB Reserved for our variables such as our state tracker, and things that i might need to add.

// Thank you https://github.com/G6EJD/ESP32_RTC_RAM

// each 4 byte, struct takes 20 bytes in total. Since we have 15KB we can take 750 sensor loggings before needing to upload.
// we will log every 5 mins, with that we get about 2.7 days. however in practice we will only log when a movement of over 50ish meters
// so we should be able to last much longer.

// HOWEVER despite the sheet saying we have 16k, it seems on 8 is free for us to use:( so we will half the number of plot points :(
const int READSIZE = 365;

typedef struct {
    float lat;
    float lon;
    float alt;
    float speed;
    long epochTimestamp;
} plotPoint;

RTC_DATA_ATTR plotPoint Readings[READSIZE];

// now we have a KB for whatever

RTC_DATA_ATTR int readingCount = 0;
RTC_DATA_ATTR enum States state = STATE_SETUP;

// using plotPoint as home doesn't make that much sense, but makes it work nice with other functions
const plotPoint HOMEPOINT = {
    0, // your home lat
    0, // your home lon
    100, // ok unchanged
    0, //ok unchanged
    0, //ok unchanged
};

// Replace with your network credentials
const char* SSID = "...";
const char* PASSWORD = "...";

const char* DEVICE_ID = "any_short_string";
const char* POST_URL = "http://fast_api_server/location/";


void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(115200);
    print_wakeup_reason();
    ss.begin(GPSBaud);

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED){
        pinMode(D0, OUTPUT);
        digitalWrite(D0,HIGH);

        Serial.print(F("Testing TinyGPSPlus library v. "));
        Serial.println(TinyGPSPlus::libraryVersion());
        Serial.println(F("by Mikal Hart"));
        Serial.println();
    } else {
        gpio_hold_dis(GPIO_NUM_1);
        pinMode(D0, OUTPUT);
        digitalWrite(D0,HIGH);
    }

    



    // WIFI!!
    WiFi.mode(WIFI_STA);
}

void loop() {
    Serial.println(F("----- main loop ------"));
    Serial.print(F("STATE CHANGE RATE: "));
    Serial.println(SECS_BETWEEN);
    if (millis() > 5000 && gps.charsProcessed() < 10) {
        Serial.println(F("No GPS detected: check wiring."));
        state = STATE_ERROR;
    } else {
        digitalWrite(LED_BUILTIN, LOW);
        plotPoint currPlace = grabReading();
        bool recordable = false;


        switch (state) {
            case STATE_SETUP:
                Serial.println(F("STATE SETUP"));
                recordable = true;
                if (haversine(currPlace, HOMEPOINT) < HOME_SIZE) {
                    state = STATE_HOME;
                } else {
                    state = STATE_TRACK;
                }
                break;
            case STATE_TRACK:
                digitalWrite(LED_BUILTIN, HIGH);
                Serial.println(F("STATE TRACK"));
                if (haversine(currPlace, getPriorEntry()) > DISTANCE_UPDATE) {
                    recordable = true;
                }
                if (haversine(currPlace, HOMEPOINT) < HOME_SIZE) {
                    state = STATE_HOME_UPLOAD;
                    SECS_BETWEEN = 10;
                }
                delay(250);
                digitalWrite(LED_BUILTIN, LOW);
                delay(250);
                break;
            case STATE_HOME_UPLOAD:
                Serial.println(F("STATE HOME_UPLOAD"));
                if (enableWiFi() && uploadEntries()) {
                    state = STATE_HOME;
                } else {
                    state = STATE_TRACK;
                }
                disableWiFi();
                break;
            case STATE_HOME:
                Serial.println(F("STATE HOME"));
                if (haversine(currPlace, HOMEPOINT) > HOME_SIZE) {
                    recordPoint(currPlace);
                    state = STATE_TRACK;
                }
                break;
            case STATE_ERROR:
                Serial.println(F("STATE ERROR"));
                while (true) {
                    delay(250);
                    digitalWrite(LED_BUILTIN, HIGH);
                    delay(250);
                    digitalWrite(LED_BUILTIN, LOW);
                }
                break;
        }
        digitalWrite(LED_BUILTIN, HIGH);

        if (recordable) {
            SECS_BETWEEN = MIN_SECS_BETWEEN;
            recordPoint(currPlace);
        } else {
            SECS_BETWEEN = min( (int) MAX_SECS_BETWEEN, (int) (SECS_BETWEEN*1.5));
        }
    }
    
    // allow for instant response upon being home:)
    if (state != STATE_HOME_UPLOAD) {
        if (SECS_BETWEEN < 90) {
            delay(1000*SECS_BETWEEN);
        } else {
            Serial.println(F("Entering Deep Sleep"));
            digitalWrite(D0,LOW);
            gpio_deep_sleep_hold_en();
            gpio_hold_en(GPIO_NUM_1);
            esp_sleep_enable_timer_wakeup(1000000ULL*SECS_BETWEEN);
            esp_deep_sleep_start();
        }
    }
}

bool recordPoint(plotPoint recording) {
    if (readingCount >= READSIZE) {
        Serial.print(F("Point storage full readingCount ="));
        Serial.print(readingCount);
        Serial.print(F(" out of" ));
        Serial.println(READSIZE);

        state = STATE_ERROR;

        return false;
    }
    Serial.println("Logged reading");

    Readings[readingCount] = recording;
    readingCount++;

    return true;
}

bool uploadEntries() {
    //just skip upload and print if small entry!
    if (readingCount < 5) {
        Serial.println(F("SMALL ENTRY DISCARDED"));
        readingCount = 0;
        return true;
    }
    Serial.println("Preparing to upload entry");
    HTTPClient http;
    http.setTimeout(60000); // 60 seconds as pulling in the photos can take some time.
    if (http.begin(POST_URL)) {
        http.addHeader("Content-Type", "application/json");

        const int staticSize = strlen("{\"event\":{\"points\":[") + strlen("],\"device_id\":\"") + strlen("\"},\"publishTraccar\":true,\"printMap\":false,\"returnPDF\":false}");

        const int readingSize = strlen("{\"time\":,\"lat\":,\"lon\":,\"acc\":2,\"speed\":,\"head\":,\"alt\":}");

        // 11*2 for lat/lon as -___.______, (len(2^64)), 
        const int readingSizeNumber = 11*4 + 20; 

        const int maxPayloadSize = staticSize + (READSIZE * (readingSize + readingSizeNumber)) + 1024;    // some extra:)

        char payload[maxPayloadSize];
        int offset = 0;

        // Start building the JSON payload
        offset += snprintf(payload + offset, maxPayloadSize - offset,
                                             "{\"event\":{\"points\":[");

        for (int i = 0; i < readingCount; i++) {
            if (i > 0) {
                offset += snprintf(payload + offset, maxPayloadSize - offset, ",");
            }
            offset += snprintf(payload + offset, maxPayloadSize - offset,
                                                 "{\"time\":%ld,\"lat\":%0.6f,\"lon\":%0.6f,\"acc\":2,\"speed\":%0.2f,\"head\":0,\"alt\":%0.2f}",
                                                 Readings[i].epochTimestamp, Readings[i].lat, Readings[i].lon, Readings[i].speed, Readings[i].alt);
        }

        offset += snprintf(payload + offset, maxPayloadSize - offset,
                                             "],\"device_id\":\"%s\"},\"publishTraccar\":true,\"printMap\":true,\"returnPDF\":false}",
                                             DEVICE_ID);

        int httpCode = http.POST(payload);    // start connection and send HTTP header
        Serial.print(F("Got HTTP code"));
        Serial.println(httpCode);
        http.end();
        if (httpCode == 200) {
            readingCount = 0;
            return true;
        }
    } else {
        Serial.println("[HTTP] Unable to connect");
        delay(1000);
    }
    return false;
}


// expects there to be at least 1 reading made
plotPoint getPriorEntry() {
    if (readingCount < 1) {
        Serial.println(F("No readings made"));
        state = STATE_ERROR;
        return Readings[0];    // may be leaked data
    }
    return Readings[readingCount - 1];
}



// Haversine
double haversine(plotPoint a, plotPoint b) {
    //Serial.println(F("COMPARE HAVERSINE POINTS"));
    //printPlotPoint(a);
    //printPlotPoint(b);
    double R = 6378;
    double dLat = radians(b.lat - a.lat);
    double dLon = radians(b.lon - a.lon);
    double c = sin(dLat / 2) * sin(dLat / 2) + cos(radians(a.lat)) * cos(radians(b.lat)) * sin(dLon / 2) * sin(dLon / 2);
    double d = 2 * atan2(sqrt(c), sqrt(1 - c));
    Serial.print(F("Distance away is (m)"));
    double distance = R * d * 1000;
    Serial.println(distance);
    return distance;
}

plotPoint grabReading() {
    Serial.println(F("Awaiting gps data..."));
    int acc_count = 0; // lets get more than one sample to make it more acc
    while (!(gps.location.isValid() && gps.date.isValid() && gps.altitude.isValid() && gps.speed.isValid()) || acc_count < 50) {
      if(ss.available() > 0 && gps.encode(ss.read())) {
          acc_count++;
          displayInfo();
          Serial.println(acc_count);
      }
    }
    Serial.println(F("Data Gained."));

    displayInfo();    
    setTime(gps.time.hour(), gps.time.minute(), gps.time.second(), gps.date.day(), gps.date.month(), gps.date.year());
    plotPoint point = {
        gps.location.lat(),
        gps.location.lng(),
        gps.altitude.meters(),
        gps.speed.kmph(),
        now(),
    };
    return point;
}

void printPlotPoint(const plotPoint& point) {
    Serial.print("Latitude: ");
    Serial.println(point.lat, 6); // Print latitude with 6 decimal places
    Serial.print("Longitude: ");
    Serial.println(point.lon, 6); // Print longitude with 6 decimal places
    Serial.print("Altitude: ");
    Serial.println(point.alt, 2); // Print altitude with 2 decimal places
    Serial.print("Speed: ");
    Serial.println(point.speed, 2); // Print speed with 2 decimal places
    Serial.print("Timestamp: ");
    Serial.println(point.epochTimestamp);
}

// inspired https://mischianti.org/esp32-practical-power-saving-manage-wifi-and-cpu-1/
bool enableWiFi(){
    WiFi.disconnect(false);  // Reconnect the network
    WiFi.mode(WIFI_STA);    // Switch WiFi off
 
    Serial.println("START WIFI");
    WiFi.begin(SSID, PASSWORD);

    for (int i = 0; i < 200; i++){
        if (WiFi.status() == WL_CONNECTED) break;
        Serial.print(".");
        delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Failed to connect to wifi");
        return false;
    }
 
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
}

void disableWiFi(){
    WiFi.disconnect(true);  // Disconnect from the network
    WiFi.mode(WIFI_OFF);    // Switch WiFi off
}

void displayInfo() {
    Serial.print(F("Location: "));
    if (gps.location.isValid()) {
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(","));
        Serial.print(gps.location.lng(), 6);
    } else {
        Serial.print(F("INVALID"));
    }

    Serial.print(F(". Date/Time: "));
    if (gps.date.isValid()) {
        Serial.print(gps.date.month());
        Serial.print(F("/"));
        Serial.print(gps.date.day());
        Serial.print(F("/"));
        Serial.print(gps.date.year());
    } else {
        Serial.print(F("INVALID"));
    }

    Serial.print(F(" "));
    if (gps.time.isValid()) {
        if (gps.time.hour() < 10) Serial.print(F("0"));
        Serial.print(gps.time.hour());
        Serial.print(F(":"));
        if (gps.time.minute() < 10) Serial.print(F("0"));
        Serial.print(gps.time.minute());
        Serial.print(F(":"));
        if (gps.time.second() < 10) Serial.print(F("0"));
        Serial.print(gps.time.second());
        Serial.print(F("."));
        if (gps.time.centisecond() < 10) Serial.print(F("0"));
        Serial.print(gps.time.centisecond());
    } else {
        Serial.print(F("INVALID"));
    }

    Serial.println();
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}
