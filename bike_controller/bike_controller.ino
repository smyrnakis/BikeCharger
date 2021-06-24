#include <Arduino.h>
#include <math.h>
#include <SoftwareSerial.h>

#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include "DHT.h"

#include "secrets.h"

#define DHTTYPE DHT11
#define DHTPIN D1

#define RX_PIN D2
#define TX_PIN D3

#define ESPLED D4       // 2
#define PCBLED D0       // 16 , LED_BUILTIN
#define ROAD_LED D7

#define REED_SENSOR D5


char access_SSID[] = ACCESS_POINT_SSID;
char access_PASS[] = ACCESS_POINT_PASS;


const unsigned long interval_LED                        = 1000;
const unsigned long interval_DHT11                      = 5000;
const unsigned long interval_dataPrint                  = 1000;
const unsigned long interval_ThingSpeakUpload           = 15000;

const unsigned long debounce_min_between_revolution     = 150;
const unsigned long delay_operational_after_revolution  = 5000;
const unsigned long delay_reset_data_after_finish       = 15000;

// unsigned long time_lastDataCalculated   = 0;
unsigned long time_preLastRevolution    = 0;
unsigned long time_lastRevolution       = 0;
unsigned long time_lastDataPrint        = 0;
unsigned long time_lastLEDblink         = 0;
unsigned long time_lastUpload           = 0;
unsigned long time_lastDHT11            = 0;


const int wheel_radius                  = 34;       // in cm
const float watt_per_revolution         = 0.46;

float humidity;
float temperature;

bool allowThingSpeak                    = true;
bool allowSerial                        = false;
bool underOperation                     = false;
bool rideDuration_printed               = true;

unsigned long usageSeconds              = 0;
int rpm                                 = 0;
int bicycleSpeed                        = 0;        // average (total distance / usage time) OR momentary (rpm to kph)
unsigned long energyProduced            = 0;
int energyInstantaneous                 = 0;
int distanceTravelled                   = 0;        // we need total rpm per ride
unsigned long rideRevolutions           = 0;

unsigned long startTime                 = 0;
unsigned long stopTime                  = 0;
unsigned long revolutionDurationMS      = 0;


// unsigned long lastTEMPevent = 0;
// unsigned long interval_lastTEMPevent = 1000;


// Software serial used to communicate with 2nd ESP8266 (internet_relay)
SoftwareSerial Serial_internet_relay(RX_PIN, TX_PIN);

ESP8266WebServer server(80);

DHT dht(DHTPIN, DHTTYPE);


float circumference_CM(int radiusCM) {                                          // output in CM
    // circumference = 2 * Pi * radius(cm)
    return 2.0 * M_PI * radiusCM;
}


int revolutions_per_minute_RPM(float revDurationMS) {
    // rpm = 60000(ms/min) / revolution_duration(ms)
    return int(60000 / revDurationMS);
}


float kilometers_per_hour(int rpm, float circumferenceCM) {
    // kph = rpm * circumference(cm) * 60(min) / 100000(cm/km)
    return circumferenceCM * rpm * 60.0 / 100000.0;
}


unsigned long usage_seconds(unsigned long startMILLIS, unsigned long stopMILLIS) {
    return (stopMILLIS - startMILLIS) / 1000;
}


// float average_speed_KPH(float distanceKM, int timeSEC) {                        // output KPH
//     return distanceKM / (timeSEC / 60.0 / 60.0);
// }


float distance_travelled_KM(unsigned long totalRev, float circumferenceCM) {    // output in KM
    // d(km) = revolutions * circumference(cm) / 100000(cm/km)
    return totalRev * circumferenceCM / 100000.0;
}


float distance_travelled_M(unsigned long totalRev, float circumferenceCM) {     // output in M
    // d(km) = revolutions * circumference(cm) / 100(cm/m)
    return totalRev * circumferenceCM / 100.0;
}


int energy_current_WATT(int rpm, float wattPerRpm) {
    // wh = rpm * W/rpm (constant)
    return rpm * wattPerRpm;
}


// TO BE FIXED
// float energy_produced_average_WH(int currentEnergyW, unsigned long rideRevs) {    
//     return currentEnergyW * rideRevs;
// }


String millisToTime(bool calcDays) {

    char outString[16];

    unsigned long millisecondsNow = millis();
    unsigned long tempTime = millisecondsNow / 1000;

    unsigned long seconds = tempTime % 60;

    tempTime = (tempTime - seconds) / 60;
    unsigned long minutes = tempTime % 60;

    tempTime = (tempTime - minutes) / 60;
    unsigned long hours = tempTime % 24;

    unsigned long days = (tempTime - hours) / 24;

    // ~~~~~~~~~~ another algorithm ~~~~~~~~~~
    // int days = n / (24 * 3600);

    // n = n % (24 * 3600);
    // int hours = n / 3600;

    // n %= 3600;
    // int minutes = n / 60 ;

    // n %= 60;
    // int seconds = n;

    if (calcDays) {
        // output:  1d 03h 42' 04"  (d HH MM SS)
        sprintf(outString, "%dd %02dh %02d' %02d\"", days,hours,minutes,seconds);
    }
    else {
        // output:  03:42:04        (HH:MM:SS)
        sprintf(outString, "%02d:%02d:%02d" ,hours,minutes,seconds);
    }

    return outString;
}


void print_data() {

    Serial.print("\n\nrevolutionDurationMS: ");
    Serial.print(revolutionDurationMS);
    Serial.println(" ms");

    Serial.print("rpm: ");
    Serial.print(rpm);
    Serial.println(" rpm");

    Serial.print("bicycleSpeed: ");
    Serial.print(bicycleSpeed);
    Serial.println(" kph");

    Serial.print("energyInstantaneous: ");
    Serial.print(energyInstantaneous);
    Serial.println(" W");

    Serial.print("energyProduced: ");
    Serial.print(energyProduced);
    Serial.println(" W");

    Serial.print("distanceTravelled: ");
    Serial.print(distanceTravelled);
    Serial.println(" m\n");

    time_lastDataPrint = millis();
}


void reset_Data() {
    rpm                     = 0;
    usageSeconds            = 0;
    bicycleSpeed            = 0;
    energyInstantaneous     = 0;
    energyProduced          = 0;
    rideRevolutions         = 0;
    distanceTravelled       = 0;

    startTime               = 0;
    stopTime                = 0;
    revolutionDurationMS    = 0;

    underOperation          = false;
}


void read_DHT11() {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();

    // if (allowSerial) {
    //     Serial.print("\nTemperature: ");
    //     Serial.print(String(temperature));
    //     Serial.println(" °C");
    //     Serial.print("Humidity: ");
    //     Serial.print(String(humidity));
    //     Serial.println(" %\n");
    // }

    time_lastDHT11 = millis();
}


void serial_sendData() {
    digitalWrite(ESPLED, LOW);
    String data_packet = String(temperature);
    data_packet += "&";
    data_packet += String(humidity);
    data_packet += "&";
    data_packet += String(underOperation);
    data_packet += "&";
    data_packet += String(bicycleSpeed);
    data_packet += "&";
    data_packet += String(distanceTravelled);
    data_packet += "&";
    data_packet += String(usageSeconds);
    data_packet += "&";
    data_packet += String(energyInstantaneous);
    data_packet += "&";
    // data_packet += String(energyProduced);
    // data_packet += "&";

    data_packet += "\r\n";

    Serial_internet_relay.print(data_packet);

    digitalWrite(ESPLED, HIGH);
    time_lastUpload = millis();
}


ICACHE_RAM_ATTR void revolution() {                         // interrupt handler

    // IF last revolution was more than 'delay_operational_after_revolution' (2") time ago
    // AND currently is NOT under operation THEN --> consider NOW as 'startTime'
    if ((millis() > time_lastRevolution + delay_operational_after_revolution) && !underOperation) {

        rideRevolutions = 0;        // reset revolutions counter
        startTime = millis();
    }

    // Debounce reed sensor (revolutions can not happen closer than 150ms between each other)
    if (millis() > time_lastRevolution + debounce_min_between_revolution) {
        underOperation = true;
        rideRevolutions += 1;

        time_preLastRevolution  = time_lastRevolution;
        time_lastRevolution     = millis();

        revolutionDurationMS    = time_lastRevolution - time_preLastRevolution;
    }

    // On every revolution, calculate ride's data
    rpm                     = revolutions_per_minute_RPM(revolutionDurationMS);
    bicycleSpeed            = kilometers_per_hour(rpm, circumference_CM(wheel_radius));
    energyInstantaneous     = energy_current_WATT(rpm, watt_per_revolution);
    energyProduced          += energyInstantaneous;
    distanceTravelled       = distance_travelled_M(rideRevolutions, circumference_CM(wheel_radius));

    // Calculate ongoing ride's duration
    if ((stopTime == 0) && underOperation) {
        usageSeconds = usage_seconds(startTime, millis());
    }
}


void setup() {
    pinMode(ESPLED, OUTPUT);
    pinMode(PCBLED, OUTPUT);
    pinMode(ROAD_LED, OUTPUT);
    pinMode(REED_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(REED_SENSOR), revolution, FALLING);
    // attachInterrupt(digitalPinToInterrupt(REED_SENSOR), revolution, RISING);

    digitalWrite(ESPLED, HIGH);
    digitalWrite(PCBLED, HIGH);
    digitalWrite(ROAD_LED, HIGH);
    
    Serial.begin(9600);                             // serial with PC (over USB)
    delay(200);

    Serial_internet_relay.begin(9600);              // serial with 2nd Arduino (internet_relay)
    delay(200);

    // WiFi.mode(WIFI_AP);
    WiFi.softAP(access_SSID, access_PASS);

    IPAddress myIP = WiFi.softAPIP();
    if (allowSerial) {
        Serial.print("\n[SUCCESS] HotSpot IP : ");
        Serial.println(myIP);
    }

    server.on("/", onConnect_default);
    server.on("/restart", onConnect_restart);
    server.on("/settings", onConnect_settings);
    server.on("/toggleThSp", onConnect_toggleThingSpeak);
    server.on("/toggleSerial", onConnect_toggleSerial);
    server.onNotFound(onConnect_notFound);

    server.begin();
    if (allowSerial) {
        Serial.println("[SUCCESS] HTTP server started");
    }
    delay(200);

    dht.begin();
    if (allowSerial) {
        Serial.println("[SUCCESS] DHT started");
    }
    delay(200);

    reset_Data();
    delay(200);
}


void loop() {
    // Handler for WiFi connections
    server.handleClient();

    // Blink LED every 1" (interval_LED)
    if (millis() > time_lastLEDblink + interval_LED) {
        digitalWrite(PCBLED, !digitalRead(PCBLED));
        time_lastLEDblink = millis();
    }

    // Read temperature & humidity every 5" (interval_DHT11)
    if (millis() > time_lastDHT11 + interval_DHT11) {
        read_DHT11();
    }

    // If last revolution was more than 2" (delay_operational_after_revolution) before, ride is over
    if ((millis() > time_lastRevolution + delay_operational_after_revolution) && underOperation) {
        underOperation = false;
        stopTime = millis();
    }

    if (underOperation) {
        digitalWrite(ROAD_LED, LOW);
        // usageSeconds = 0;

        // Print calculated data every 1" (interval_dataPrint) when under operation
        if (millis() > time_lastDataPrint + interval_dataPrint) {
            if (allowSerial) {
                print_data();
            }
        }

        // flag for printing ride duration (sec) after ride is over
        rideDuration_printed = false;
    }
    else {
        digitalWrite(ROAD_LED, HIGH);

        // Calculate and print ride duration (sec) after ride is over
        usageSeconds = usage_seconds(startTime, stopTime);
        if (!rideDuration_printed && (startTime != 0)) {
            if (allowSerial) {
                Serial.print("Ride duration: ");
                Serial.print(usageSeconds);
                Serial.println(" sec");
            }
            rideDuration_printed = true;
        }

        // Reset all counters 15" (delay_reset_data_after_finish) after ride is over
        if (millis() > stopTime + delay_reset_data_after_finish) {
            reset_Data();
        }
    }

    // Send data to ThingSpeak (2nd Arduino - internet_relay) every 15" (interval_ThingSpeakUpload)
    if (millis() > time_lastUpload + interval_ThingSpeakUpload) {
        if (allowThingSpeak) {
            serial_sendData();
        }
    }

    // Print to Serial (computer) whatever arrives from 2nd Arduino (internet_relay)
    if (Serial_internet_relay.available()) {
        if (allowSerial) {
            Serial.write(Serial_internet_relay.read());
        }
    }
    delay(2);
}


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ HTML code ~~~~~~~~~~~~~~~~~~~~~~~
String HTML_LANDING_PAGE() {
    String html_page = "<!DOCTYPE html> <html>\n";
    html_page += "<meta http-equiv=\"refresh\" content=\"2\" >\n";
    html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    html_page += "<link rel=\"icon\" href=\"data:,\">\n";
    html_page += "<title>BikeCharger</title>\n";
    html_page += "<style>\n";
    html_page += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }\n";
    html_page += "body { margin-top: 50px; color: white; background: black;}\n";
    // html_page += "h1 { color: #B4F9F3; margin: 50px auto 30px; }\n";
    html_page += "p { font-size: 24px; }\n";
    // html_page += "p { font-size: 24px; color: #B4F9F3; margin-bottom: 10px; }\n";
    // html_page += "table, table td { border: 0px solid #cccccc; }\n";
    html_page += "table, table td { text-align: center; vertical-align: middle; padding-top: 5px; padding-bottom: 5px; }\n";
    // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;\n";
    // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px; text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer; }\n";
    // html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;\n";
    html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: auto;\n";
    html_page += "text-decoration: none; text-align: center; font-size: 20px; margin: 2px; cursor: pointer; }\n";
    html_page += ".button2 { background-color: #77878A; }\n";
    html_page += ".button3 { background-color: #ff3300; }\n";

    html_page += "</style>\n";
    html_page += "</head>\n";

    html_page += "<body>\n";
    html_page += "<div id=\"webpage\">\n";
    html_page += "<h1>BikeCharger</h1>\n";
    html_page += "<br />\n";

    html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";

    html_page += "<tr>\n";
    html_page += "<td>Temperature:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)temperature;
    html_page += " &#176C</td>";                            // '°' is '&#176' in HTML
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Humidity:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)humidity;
    html_page += " %</td>";
    html_page += "</tr>\n";

    html_page += "</table>\n";

    html_page += "<br />\n";

    html_page += "<h2>Current Ride</h2>\n";
    // html_page += "<br />\n";

    html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";

    html_page += "<tr>\n";
    html_page += "<td>Speed:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)bicycleSpeed;
    html_page += " kph</td>";
    html_page += "</tr>\n";
    
    html_page += "<tr>\n";
    html_page += "<td>Revolutions:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)rpm;
    html_page += " rpm</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Distance:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)distanceTravelled;
    html_page += " m</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Duration:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)usageSeconds;
    html_page += " sec</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Energy (current):</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)energyInstantaneous;
    html_page += " Watt</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Energy (ride):</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)energyProduced;
    html_page += " Watt</td>";
    html_page += "</tr>\n";

    html_page += "</table>\n";

    html_page += "<br />\n";

    html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";

    html_page += "<tr>\n";
    // html_page += "<td colspan=\"2\"><p><a href=\"/settings\"><button class=\"button\">SETTINGS</button></a></p></td>";
    html_page += "<td colspan=\"2\"><a href=\"/settings\"><button class=\"button\">SETTINGS</button></a></td>";
    html_page += "</tr>\n";

    html_page += "</table>\n";

    html_page += "<br />\n";

    html_page += "</div>\n";
    html_page += "</body>\n";
    html_page += "</html>\n";

    return html_page;
}

String HTML_SETTINGS_PAGE() {
    String html_page = "<!DOCTYPE html> <html>\n";
    html_page += "<meta http-equiv=\"refresh\" content=\"2\" >\n";
    html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    html_page += "<link rel=\"icon\" href=\"data:,\">\n";
    html_page += "<title>BikeCharger</title>\n";
    html_page += "<style>\n";
    html_page += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }\n";
    html_page += "body { margin-top: 50px; color: white; background: black;}\n";
    // html_page += "h1 { color: #B4F9F3; margin: 50px auto 30px; }\n";
    html_page += "p { font-size: 24px; }\n";
    // html_page += "p { font-size: 24px; color: #B4F9F3; margin-bottom: 10px; }\n";
    // html_page += "table, table td { border: 0px solid #cccccc; }\n";
    html_page += "table, table td { text-align: center; vertical-align: middle; padding-top: 5px; padding-bottom: 5px; }\n";
    // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px;\n";
    // html_page += ".button { background-color: #195B6A; border: none; color: white; padding: 16px 50px; text-decoration: none; text-align: center; font-size: 30px; margin: 2px; cursor: pointer; }\n";
    // html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: 130px;\n";
    html_page += ".button { background-color: #195B6A; border: none; color: white; height: 50px; width: auto;\n";
    html_page += "text-decoration: none; text-align: center; font-size: 20px; margin: 2px; cursor: pointer; }\n";
    html_page += ".button2 { background-color: #77878A; }\n";
    html_page += ".button3 { background-color: #ff3300; }\n";

    html_page += "</style>\n";
    html_page += "</head>\n";

    html_page += "<body>\n";
    html_page += "<div id=\"webpage\">\n";
    html_page += "<h1>SETTINGS</h1>\n";
    html_page += "<br />\n";
    // html_page += "<p>Under construction</p>";

    // html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";
    html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:16px\">\n";

    html_page += "<tr>\n";
    html_page += "<td colspan=\"2\">RUNTIME</td>";
    html_page += "</tr>\n";
    html_page += "<tr>\n";
    html_page += "<td>millis():</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += String(millis());
    html_page += "</td>";
    html_page += "</tr>\n";
    html_page += "<tr>\n";
    html_page += "<td>Up time (millis):</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += millisToTime(true);
    html_page += "</td>";
    html_page += "</tr>\n";

    html_page += "<tr><td colspan=\"2\"> </td></tr>\n";

    html_page += "<tr>\n";
    if (allowThingSpeak) {
        html_page += "<td><a href=\"/toggleThSp\"><button style=\"width:100%\" class=\"button\">ThingSpeak</button></a></td>";
    } else {
        html_page += "<td><a href=\"/toggleThSp\"><button style=\"width:100%\" class=\"button button2\">ThingSpeak</button></a></td>";
    }
    if (allowSerial) {
        html_page += "<td><a href=\"/toggleSerial\"><button style=\"width:100%\" class=\"button\">Serial</button></a></td>";
    } else {
        html_page += "<td><a href=\"/toggleSerial\"><button style=\"width:100%\" class=\"button button2\">Serial</button></a></td>";
    }
    html_page += "</tr>\n";
    html_page += "<tr><td colspan=\"2\"><a href=\"/restart\"><button style=\"width:100%\" class=\"button button3\">RESTART</button></a></td></tr>\n";
    html_page += "<tr><td colspan=\"2\"><a href=\"/\"><button style=\"width:100%\" class=\"button\">back</button></a></td></tr>\n";

    html_page += "</table>\n";

    html_page += "</div>\n";
    html_page += "</body>\n";
    html_page += "</html>\n";

    return html_page;
}

String HTML_REFRESH_TO_SETTINGS() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/settings\">";
    html_page += "</head>";

    return html_page;
}

String HTML_REFRESH_TO_ROOT() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/\">";
    html_page += "</head>";

    return html_page;
}

String HTML_NOT_FOUND() {
    String html_page = "<!DOCTYPE html> <html>\n";
    html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    html_page += "<title>RJD Monitor</title>\n";
    html_page += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
    html_page += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
    html_page += "p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
    html_page += "</style>\n";
    html_page += "</head>\n";
    html_page += "<body>\n";
    html_page += "<div id=\"webpage\">\n";
    html_page += "<h1>You know this 404 thing ?</h1>\n";
    html_page += "<p>Sorry, what you asked can not be found... :'( </p>";
    html_page += "</div>\n";
    html_page += "</body>\n";
    html_page += "</html>\n";

    return html_page;
}
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ web pages ~~~~~~~~~~~~~~~~~~~~~~~
void onConnect_default() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_LANDING_PAGE());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_settings() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_SETTINGS_PAGE());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_toggleThingSpeak() {
    digitalWrite(ESPLED, LOW);
    allowThingSpeak = !allowThingSpeak;
    // refreshToRoot();
    refreshToSettings();
    digitalWrite(ESPLED, HIGH);
}

void onConnect_toggleSerial() {
    digitalWrite(ESPLED, LOW);
    allowSerial = !allowSerial;
    // refreshToRoot();
    refreshToSettings();
    digitalWrite(ESPLED, HIGH);
}

void onConnect_notFound(){
    digitalWrite(ESPLED, LOW);
    server.send(404, "text/html", HTML_NOT_FOUND());
    digitalWrite(ESPLED, HIGH);
}

void refreshToRoot() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_REFRESH_TO_ROOT());
    digitalWrite(ESPLED, HIGH);
}

void refreshToSettings() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_REFRESH_TO_SETTINGS());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_restart() {
    refreshToRoot();
    delay(3000);
    ESP.restart();
}
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~