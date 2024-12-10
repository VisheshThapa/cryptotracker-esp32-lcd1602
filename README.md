# cryptotracker-esp32-lcd1602

Crypto tracker for ESP32 conncted to an 1602 LCD display.

Change the values for wifi ssid, password and coingecko api key. Build files are for esp32s3 so it is recommended to clean the build file before uploading to your esp32

# Wiring
SCL is connected to GPIO 13 and SDA to GPIO 14, you can change the values in the file before compiling.
Depending on the serial-to-parallel IC chip your address is going to be diffrent.
PCF8574T: 0x27
PCF8574AT: 0x3F

This projects default is 0x3F

# Build and run the file in Linux:
Replace the path where esp32 is mounted on your computer. 
It can be checked with ``` ls /dev/tty*``` which for me is ttyACM0
```
idf.py build
idf.py flash /dev/ttyACM0/ monitor
```

