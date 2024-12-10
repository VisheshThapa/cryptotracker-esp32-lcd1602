# cryptotracker-esp32-lcd1602

Crypto tracker for ESP32 conncted to an 1602 LCD display.

Change the values for wifi ssid, password and coingecko api key. Build files are for esp32s3 so it is recommended to clean the build file before uploading to your esp32

# Build and run the file in Linux:
Replace the path where esp32 is mounted on your computer. 
It can be checked with ``` ls /dev/tty*``` which for me is ttyACM0
```
idf.py build
idf.py flash /dev/ttyACM0/ monitor
```

