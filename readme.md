# BikeCharger

> An Arduino based bicycle computer meant to be installed on stationary bicycles.
> 
> Rider can connect on Arduino #1 *(bike_controller)* WiFi hotspot "`BikeCharger`" and see live the ride's details.
> 
> Arduino #1 communicates over Serial protocol with Arduino #2 *(internet_relay)*. The 2nd Arduino is in charge of connecting to the Internet and uploading to [ThingSpeak](https://thingspeak.com/channels/1408003) the ride's data.

<br>

## bike_controller

Based on ESP8266. The microcontroller operates offline with the following I/Os:
- INPUTS
    - DHT11 temperature & humidity sensor *(pin D1)*
    - magnetic reed switch (bicycle sensor) *(pin D5)*
- OUTPUTS
    - LED light *(pin D7)*
    - ESP LED (WiFi LED) *(pin D4 - Arduino: 2)*
    - PCB LED (LED_BUILTIN) *(pin D0 - Arduino: 16)*
- SERIAL (software)
    - Rx *(pin D2)*
    - Tx *(pin D3)*

<br>

### Operation

On startup, the ESP8266 creates a local hotspot with the following details:

| SSID | PASSWORD |
|:---:|:---:|
|BikeCharger|letsbike|

When a user wants to operate the bicycle, they should connect on this hotspot in order to be able to see their data.

When everything is OK, the PCB LED is blinking with a period of 1 second.

Every 5 seconds, the Arduino reads the *temperature* and the *humidity* using the **DHT11** sensor.

The magnetic reed switch is checked continuously (through an interrupt function). Every time that the wheel makes a full turn, Arduino will calculate the following:
- `revolutionDurationMS` - time needed for a full wheel revolution in *milliseconds*
- `rpm` - revolutions per minute
- `bicycleSpeed` - ground speed in *KPM* (kilometres per hour)
- `energyInstantaneous` - current energy production in *Watt*
- `distanceTravelled` - ride's distance travelled in *meters*
- `usageSeconds` - current ride's duration in *seconds*

The above are calculated in the *interrupt function* `ICACHE_RAM_ATTR void revolution()`.

As long as the bike is **under operation** *(check next chapter - "Limitations")*, then the data is calculated and sent via the Software Serial `Serial_internet_relay` to the 2nd Arduino *(internet_relay)*.

<br>

### Limitations

The revolutions can not be *faster* than **150 ms**.

Additionally, if there's no revolution for **2"** it considers the bicycle as *non-used*.


For a **34 cm** radius wheel (*circumference* of **213 cm**), the minimum / maximum *RPM* and *KPH* are:

| | RPM | KPM |
|:---:|:---:|:---:|
|MIN|30|3.8|
|MAX|400|51.1|

<br>

### Calculations and the Math

To keep track of time, the `millis()` function is used. This function returns the *current millisecond* since the Arduino was powered up.

It is of `unsigned long` type, thus it can reach up to **4,294,967,295 milliseconds** which is approximately **49 days, 17 hours and 2 minutes** (or 7.1 weeks).

<br>

The Arduino knows the **radius** of the wheel (in our case 34 cm) and can measure the time between each **revolution** (in milliseconds).

From the radius, we can calculate the **circumference** of the wheel in CM:
    
    C = 2 * pi * R

    pi  = 3.1415
    R   = radius in CM

<br>

From the duration of every revolution, we can calculate the **revolutions per minute (RPM)**:

    RPM = 60000 / revolution_duration

    60000               : milliseconds in a minute
    revolution_duration : in milliseconds

<br>

Knowing the RPM and the circumference, we can calculate the **speed** in kilometres per hour (KPH):

    KPH = rpm * C * 60 / 100000
    
    C       : circumference in CM
    60      : minutes (to calculate for an hour)
    100000  : centimetres in a kilometre

<br>

Using the total number of revolutions (calculated inside the interrupt function) and the circumference (in CM), we can calculate the **distance travelled** in meters:

    D = revolutions * C / 100

    revolutions : total number of revolutions until now
    C           : circumference in CM
    100         : centimetres in a meter

<br>

Average cyclist produce approximately **310 watts in 7.5 minutes** [[source](https://bestsportslounge.com/watts-produced-cycling)].

We calculate an average for Watt/revolution:
- 310 W  /  7.5 min  =  41.3 W/min
- average RPM = 90
- 41.3 W/min  /  90 rpm  =  0.46 W/revolution

From the RPM and using a constant for the watt per revolution, we can calculate the **instantaneous energy production** in WATT:

    WATT = rpm * W/rpm (constant)
    
    rpm     : revolutions per minute
    W/rpm   : 0.46 Watt (constant)

<br>
<br>

## internet_relay

Based on ESP8266. The microcontroller is using the following I/Os:
- OUTPUTS
    - ESP LED (WiFi LED) *(pin D4 - Arduino: 2)*
    - PCB LED (LED_BUILTIN) *(pin D0 - Arduino: 16)*
- SERIAL (software)
    - Rx *(pin D2)*
    - Tx *(pin D3)*

<br>

### Operation

On startup, the ESP8266 creates a local hotspot with the following details:

| SSID | PASSWORD |
|:---:|:---:|
|BikeCharger_online|letsbike|

The administrator needs to connect on this hotspot and using the on-screen guidance, connect the Arduino to an available WiFi network.

When everything is OK, the PCB LED is blinking with a period of 1 second.

The ESP8266 is waiting for serial data to become available (either on the USB serial port or on the Software Serial `Serial_bike` which connects the two Arduinos together).

When data is available, the incoming String is split using the "`&`" character as a delimiter.

The expected order of incoming data is the following:
- `temperature` - the ambient temperature in °C
- `humidity` - the ambient humidity in %
- `underOperation` - 0/1 (active ride or bike not used)
- `bicycleSpeed` - speed in kilometres per hour
- `distanceTravelled` - ride's distance in meters
- `usageSeconds` - ride's duration in seconds
- `energyInstantaneous` - current energy production in Watt

**NOTE:** the incoming string must end with the "`&`" character. Example: "`28&37&1&25&243&120&65&`".

On serial data reception, after striping the String and assigning its value to an array cell, the function `int call_thingSpeak()` is called. This function will upload the data to the [ThingSpeak](https://thingspeak.com/channels/1408003) using a `GET` request.

The URL for the above mentioned String example will be:
`https://api.thingspeak.com/update?api_key=XXXXXXXXXX&field1=28&field2=37&field3=1&field4=25&field5=243&field6=120`

The `API_KEY` is stored in the file `secrets.h` with the name "`THINGSP_WR_APIKEY`".


<br>

### Limitations

The Arduino needs a working WiFi in order to upload data. If there is no WiFi connection, in will reboot every 5 seconds.

Communication between the two Arduinos ("bike_controller" and "internet_relay") is not bidirectional. In the unlike event that data upload to ThingSpeak fails, the "bike_controller" will not be notified.

<br>

* * *

## Functions, Libraries and Resources

### Resources

- [ESP8266 web server](https://randomnerdtutorials.com/esp8266-nodemcu-access-point-ap-web-server/)
- [Interrupts](https://randomnerdtutorials.com/interrupts-timers-esp8266-arduino-ide-nodemcu/)

### Libraries

- [math.h](https://www.nongnu.org/avr-libc/user-manual/group__avr__math.html)
- [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor)
- [Adafruit DHT](https://github.com/adafruit/DHT-sensor-library)

### Function

- [SoftwareSerial](https://www.arduino.cc/en/Reference/SoftwareSerial)
- [.toCharArray](https://arduinogetstarted.com/reference/arduino-string-tochararray)
- [strtok_r](https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/)


