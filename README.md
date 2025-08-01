# uSDT - Universal Software Defined Transmitter

![alt text](https://github.com/UR4URV/uSDT/blob/main/img/uSDT_7.jpg)

This device can be used as a transmitter for an SDR receiver (for example ATS-20, RTL-SDR, etc.)

This device is built on the ESP32C3 microcontroller (Module ESP32C3 SuperMini).

To generate the RF signal, a module based on SI5351 was used.

Time synchronization is implemented on the RTC DS1307 module. Also, a DS18B20 temperature sensor can be installed on this module.

At the moment, the device only works with digital communication modes (FT8, WSPR). 

Added CAT interface (KENWOOD TS-480) for device management e.g. with WSJT-X.

Added web interface for device management.

Update the file `uSDT_v1.0.ino` your WiFi connection details and specify your CALLSIGN and QTH.

```c
const char *ssid = "<...>";     //WiFi SSID
const char *password = "<...>"; //WiFi password

char callsign[10] = "<...>";    //Your callsign
char location[10] = "<...>";    //Your QTH
```

A library from the https://github.com/kgoba/ft8_lib repository was used to encode FT8.

In the ArduinoIDE environment, the following has been added to the board manager: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

## Electrical scheme

![alt text](https://github.com/UR4URV/uSDT/blob/main/uSDT_v1.0_scheme.png)


## Main board

![alt text](https://github.com/UR4URV/uSDT/blob/main/main_board/uSDT_main_board.jpg)

![alt text](https://github.com/UR4URV/uSDT/blob/main/img/uSDT_1.jpg)

## Front board

![alt text](https://github.com/UR4URV/uSDT/blob/main/front_board/uSDT_front_board.jpg)

![alt text](https://github.com/UR4URV/uSDT/blob/main/img/uSDT_4.jpg)

## Web interface

![alt text](https://github.com/UR4URV/uSDT/blob/main/img/uSDT_web_interface.png)

