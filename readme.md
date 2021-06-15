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

    uploaded to [ThingSpeak](https://thingspeak.com/channels/1408003) every 15 seconds.

<br>

### Limitations

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


