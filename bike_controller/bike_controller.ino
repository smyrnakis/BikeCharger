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
const unsigned long interval_DataCalculator             = 500;
const unsigned long interval_ThingSpeakUpload           = 15000;

const unsigned long debounce_min_between_revolution     = 150;
const unsigned long delay_operational_after_revolution  = 2000;

unsigned long time_lastDataCalculated   = 0;
unsigned long time_preLastRevolution    = 0;
unsigned long time_lastRevolution       = 0;
unsigned long time_lastLEDblink         = 0;
unsigned long time_lastUpload           = 0;
unsigned long time_lastDHT11            = 0;


const int wheel_radius                  = 34;       // in CM
const float kWh_per_revolution          = 0.05;

float humidity;
float temperature;

bool underOperation                     = false;


unsigned long usageSeconds              = 0;
int rpm                                 = 0;
int bicycleSpeed                        = 0;        // average (total distance / usage time) OR momentary (rpm to kpm)
int energyProduced                      = 0;
int distanceTravelled                   = 0;        // we need total rpm per ride
unsigned long rideRevolutions           = 0;

unsigned long startTime                 = 0;
unsigned long stopTime                  = 0;
unsigned long revolutionDurationMS      = 0;


unsigned long lastTEMPevent = 0;
unsigned long interval_lastTEMPevent = 1000;


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
    // kpm = rpm * circumference(cm) * 60(min) / 100000(cm/km)
    return circumferenceCM * rpm * 60.0 / 100000.0;
}


int usage_seconds(unsigned long startMILLIS, unsigned long stopMILLIS) {
    return (stopMILLIS - startMILLIS) / 1000;
}


float average_speed_KPH(float distanceKM, int timeSEC) {                        // output KPH
    return distanceKM / (timeSEC / 60.0 / 60.0);
}


float distance_travelled_KM(unsigned long totalRev, float circumferenceCM) {    // output in KM
    // d(km) = revolutions * circumference(cm) / 100000(cm/km)
    return totalRev * circumferenceCM / 100000.0;
}


float distance_travelled_M(unsigned long totalRev, float circumferenceCM) {     // output in M
    // d(km) = revolutions * circumference(cm) / 100(cm/m)
    return totalRev * circumferenceCM / 100.0;
}


float energy_produced_KWH(int rpm, float kwhPerRpm) {
    // kwh = rpm * kWh/rpm (constant)
    return rpm * kwhPerRpm;
}


ICACHE_RAM_ATTR void revolution() {                         // interrupt handler

    // IF last revolution was more than 'delay_operational_after_revolution' (2.5") time ago
    // AND currently is NOT under operation THEN --> consider NOW as 'startTime'
    if ((millis() > time_lastRevolution + delay_operational_after_revolution) && !underOperation) {

        rideRevolutions = 0;        // reset revolutions counter
        startTime = millis();
    }

    // Debounce reed sensor (revolutions can not happen closer than 100ms between each other)
    if (millis() > time_lastRevolution + debounce_min_between_revolution) {
        underOperation = true;
        rideRevolutions += 1;

        time_preLastRevolution  = time_lastRevolution;
        time_lastRevolution     = millis();

        revolutionDurationMS    = time_lastRevolution - time_preLastRevolution;

        rpm                     = revolutions_per_minute_RPM(revolutionDurationMS);
        bicycleSpeed            = kilometers_per_hour(rpm, circumference_CM(wheel_radius));
        energyProduced          = energy_produced_KWH(rpm, kWh_per_revolution);
        distanceTravelled       = distance_travelled_M(rideRevolutions, circumference_CM(wheel_radius));
    }
}


void get_sensorData() {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();

    // Serial.print("Temperature: ");
    // Serial.print(String(temperature));
    // Serial.println(" °C");
    // Serial.print("Humidity: ");
    // Serial.print(String(humidity));
    // Serial.println(" %");

    Serial.print("\nrevolutionDurationMS: ");
    Serial.print(revolutionDurationMS);
    Serial.println(" MS");

    Serial.print("usageSeconds: ");
    Serial.print(usageSeconds);
    Serial.println(" sec");

    Serial.print("rpm: ");
    Serial.print(rpm);
    Serial.println(" rpm");

    Serial.print("bicycleSpeed: ");
    Serial.print(bicycleSpeed);
    Serial.println(" kph");

    Serial.print("energyProduced: ");
    Serial.print(energyProduced);
    Serial.println(" kWh");

    Serial.print("distanceTravelled: ");
    Serial.print(distanceTravelled);
    Serial.println(" m");

    lastTEMPevent = millis();
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
    
    Serial.begin(9600);                                   // serial over USB (with PC)
    delay(200);

    Serial_internet_relay.begin(9600);                    // serial with "internet_relay"
    delay(200);

    // WiFi.mode(WIFI_AP);
    WiFi.softAP(access_SSID, access_PASS);

    IPAddress myIP = WiFi.softAPIP();
    Serial.print("\n[SUCCESS] HotSpot IP : ");
    Serial.println(myIP);

    server.on("/", onConnect_default);
    server.on("/restart", onConnect_restart);
    server.on("/settings", onConnect_settings);
    server.on("/about", onConnect_about);
    server.onNotFound(onConnect_notFound);

    server.begin();
    Serial.println("[SUCCESS] HTTP server started");
    delay(200);

    dht.begin();
    Serial.println("[SUCCESS] DHT started");
    delay(200);
}


void loop() {
    server.handleClient();

    if ((millis() > time_lastRevolution + delay_operational_after_revolution) && underOperation) {
        underOperation = false;
        stopTime = millis();
        // Serial.println("[DEB] underOperation=false");
    }

    if (underOperation) {

        // digitalWrite(PCBLED, LOW);
        digitalWrite(ROAD_LED, LOW);
    }
    else {
        // more stuff
        // digitalWrite(PCBLED, HIGH);
        digitalWrite(ROAD_LED, HIGH);
    }

    if (underOperation) {
        usageSeconds = 0;
    }
    else {
        usageSeconds = (stopTime - startTime) / 1000;
    }

    if (millis() > lastTEMPevent + interval_lastTEMPevent) {
        get_sensorData();
    }
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
    html_page += "<h2>Current Ride</h2>\n";
    // html_page += "<br />\n";

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


    html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";

    html_page += "<tr>\n";
    html_page += "<td>Speed:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)rpm;
    html_page += " rpm</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Speed:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)bicycleSpeed;
    html_page += " kph</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Distance:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)distanceTravelled;
    html_page += " km</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Energy produced:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)energyProduced;
    html_page += " kWh</td>";
    html_page += "</tr>\n";

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


    html_page += "<table style=\"margin-left:auto; margin-right:auto; allign:center; font-size:20px\">\n";

    html_page += "<tr>\n";
    html_page += "<td>Speed:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)rpm;
    html_page += " rpm</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Speed:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)bicycleSpeed;
    html_page += " kph</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Distance:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)distanceTravelled;
    html_page += " km</td>";
    html_page += "</tr>\n";

    html_page += "<tr>\n";
    html_page += "<td>Energy produced:</td>";
    html_page += "<td style=\"padding-left: 10px;\">";
    html_page += (String)energyProduced;
    html_page += " kWh</td>";
    html_page += "</tr>\n";

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
    html_page += "<p>What you asked can not be found... :'( </p>";
    html_page += "</div>\n";
    html_page += "</body>\n";
    html_page += "</html>\n";

    return html_page;
}
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ web pages ~~~~~~~~~~~~~~~~~~~~~~~
void onConnect_default() {
    digitalWrite(ESPLED, LOW);
    get_sensorData();
    server.send(200, "text/html", HTML_LANDING_PAGE());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_settings() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_SETTINGS_PAGE());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_about() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/plain", "Bike and save energy! (C)");
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