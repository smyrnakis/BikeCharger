#include <Arduino.h>
#include <SoftwareSerial.h>

#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

#include "secrets.h"


#define RX_PIN D2
#define TX_PIN D3

#define ESPLED D4       // 2
#define PCBLED D0       // 16 , LED_BUILTIN


char defaultSSID[]      = WIFI_DEFAULT_SSID;
char defaultPASS[]      = WIFI_DEFAULT_PASS;
char apiKeyThingSpeak[] = THINGSP_WR_APIKEY;


char serialDataDelimiter[] = "&";
// char sz[] = "temp;humidity;weight";
char * receivedData[3];
// int arrayIndex;

const unsigned long interval_LED = 1000;

unsigned long lastLEDblinkTime = 0;


// Software serial used to communicate with 1st ESP8266 (bike_controller)
SoftwareSerial Serial_bike(RX_PIN, TX_PIN);


// WebClient and HTTPS requests handler
WiFiClientSecure clientHttps;
HTTPClient remoteClient;


int call_thingSpeak() {

    digitalWrite(ESPLED, LOW);

    // GET https://api.thingspeak.com/update?api_key=XXXXX&field1=X     // "184.106.153.149" or api.thingspeak.com
    String thinkSpeakAPIurl = "https://api.thingspeak.com";
    thinkSpeakAPIurl += "/update?api_key=";
    thinkSpeakAPIurl += (String)apiKeyThingSpeak;
    // if (!isnan(receivedData[0])) {
        thinkSpeakAPIurl +="&field1=";
        thinkSpeakAPIurl += receivedData[0];
    // }
    // if (!isnan(receivedData[1])) {
        thinkSpeakAPIurl +="&field2=";
        thinkSpeakAPIurl += receivedData[1];
    // }

    clientHttps.setInsecure();
    clientHttps.connect("https://api.thingspeak.com", 443);
    remoteClient.begin(clientHttps, thinkSpeakAPIurl);

    int httpCode = remoteClient.GET();
    // String payload = remoteClient.getString();

    // if (httpCode == 200) {
    //     Serial.println("[SUCCESS] contacting ThingSpeak");
    // }
    // else {
    //     Serial.print("[DEB] ThingSpeak response ");
    //     Serial.print(String(httpCode));
    //     Serial.print(" | ");
    //     Serial.println(String(payload));
    // }

    digitalWrite(ESPLED, HIGH);
    return httpCode;
}


void setup() {
    pinMode(ESPLED, OUTPUT);
    pinMode(PCBLED, OUTPUT);

    digitalWrite(ESPLED, HIGH);
    digitalWrite(PCBLED, HIGH);
    
    Serial.begin(9600);                                   // serial over USB (with PC)
    delay(200);

    Serial_bike.begin(9600);                              // serial with "bike_controller"
    delay(200);

    WiFiManager wifiManager;
    //wifiManager.resetSettings();                        // uncomment and run in order to reset
    wifiManager.setConfigPortalTimeout(600);
    wifiManager.autoConnect(defaultSSID, defaultPASS);

    Serial.print("\n[SUCCESS] connecting to WiFi. Local IP : ");
    Serial.println(WiFi.localIP());
    Serial_bike.print("[SUCCESS] connecting to WiFi. Local IP : ");
    Serial_bike.println(WiFi.localIP());

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("[ERROR] connecting on WiFi. System will reboot in 5\" ...");
        Serial_bike.println("[ERROR] connecting on WiFi. System will reboot in 5\" ...");
        delay(5000);
        ESP.restart();
    }
}


void loop() {

    // checking WiFi connection --> reboot if no WiFi
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("[ERROR] connecting on WiFi. System will reboot in 5\" ...");
        Serial_bike.println("[ERROR] connecting on WiFi. System will reboot in 5\" ...");
        delay(5000);
        ESP.restart();
    }

    // blink LED
    if (millis() > lastLEDblinkTime + interval_LED) {
        digitalWrite(PCBLED, !digitalRead(PCBLED));
        lastLEDblinkTime = millis();
    }

    // check for incoming Serial data (Serial_bike)
    if (Serial_bike.available()) {

        String incomingSerialData;

        while (Serial_bike.available()) {

            incomingSerialData = Serial_bike.readStringUntil('\r\n');

            // if ((String(incomingSerialData)).indexOf('&') > 0) {
            if ((String(incomingSerialData)).indexOf(serialDataDelimiter) > 0) {

                // Convert from String Object to String
                // char buffer[sizeof(sz)];
                char buffer[incomingSerialData.length() + 1];
                incomingSerialData.toCharArray(buffer, sizeof(buffer));

                char * p = buffer;
                char * extractedItem;
                short arrayIndex = 0;

                // while ((extractedItem = strtok_r(p, "&", & p)) != NULL) {
                while ((extractedItem = strtok_r(p, serialDataDelimiter, & p)) != NULL) {
                    receivedData[arrayIndex] = extractedItem;
                    ++arrayIndex;
                }

                Serial.println("[SUCCESS] receiving sensor data from 'bike_controller'");
                Serial_bike.println("[SUCCESS] receiving sensor data from 'bike_controller'");


                int returnCodeThingSpeak;
                returnCodeThingSpeak = call_thingSpeak();

                Serial.print("[DEB] ThingSpeak return code: ");
                Serial.println(returnCodeThingSpeak);
                Serial_bike.print("[DEB] ThingSpeak return code: ");
                Serial_bike.println("returnCodeThingSpeak");
            }
        }
    }


    // listen for incoming Serial Data (from USB) and forward it to Software Serial (bike_controller)
    if ( Serial.available() ) {
        Serial_bike.write( Serial.read() );
    }
}