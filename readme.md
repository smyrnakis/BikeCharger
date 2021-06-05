# BikeCharger

## bike_controller

Based on ESP8266. The microcontroller operates offline with the following IOs:
- INPUTS
    - DHT11 temperature & humidity sensor
    - magnetic reed switch (bicycle sensor)
- OUTPUTS
    - LED light

### Operation

The ESP8266 checks the magnetic reed switch continuously. If there's no change during **2.5"** it considers the bicycle as *non-used*.

The microcontroller creates a local WiFi hotspot. Connection details:
| SSID | PASSWORD |
|:---:|:---:|
|BikeCharger|letsbike|

When a user wants to operate the bicycle, they should connect on the above-mentioned hotspot in order to be able to see their data.

<br>

### Data collected

| NAME | UNIT |
|:---:|:---:|
|Ambient temperature|°C|
|Ambient humidity|%|
|Wheel speed|rpm|
|Usage time|sec|

<br>

### Data calculated

| NAME | UNIT |
|:---:|:---:|
|Bicycle speed (avg)|kph|
|Distance travelled|km|
|Energy produced|kWh|

<br>

## internet_relay


<br>

* * *

## Functions, Libraries and Resources

### Resources

- [ESP8266 web server](https://randomnerdtutorials.com/esp8266-nodemcu-access-point-ap-web-server/)

### Libraries

- [math.h](https://www.nongnu.org/avr-libc/user-manual/group__avr__math.html)
- [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor)
- [Adafruit DHT](https://github.com/adafruit/DHT-sensor-library)

### Function

- [SoftwareSerial](https://www.arduino.cc/en/Reference/SoftwareSerial)
- [.toCharArray](https://arduinogetstarted.com/reference/arduino-string-tochararray)
- [strtok_r](https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/)


