# M5STACK CLOCK

This is a simple clock build with a M5 stack Air quality kit.
It will show you the current pm25, humidity, temperature and weather.


# Configuration & Build

This project is using Platform io and Espidf. you need first install platformio.

Then you have to create account on [https://ipgeolocation.io](https://ipgeolocation.io) and retreive an API key

Copy in the menu config your api key (Component Config -> IP Geolocation Io -> api key)

```sh
 platformio run --target menuconfig --environment m5stack-core-esp32
```

then build
```sh
platformio run
```

# Upload

Change `upload_port`  in `platformio.ini` according to your configuration, then you can run
```sh
platformio run --target upload
```
## Wifi Configuration

After first time you upload the build onto the board you have to set up the Wifi.
First you have to connect to the wifi `esp32`with password `esp32pwd`.
Then on a webbrowser you can go on 10.10.0.1 and follow the wifi configuration.
## Build Option to set up with Menu config

- CONFIG_CLOCK_BRIGHTNESS_AUTO: Automatic Brightness ajustment with an Ambient Light Sensor True by default
- CONFIG_CLOCK_BRIGHTNESS_DEFAULT_VALUE: default brightness value [1-255]

## Hardware
- M5 stack [PM2.5 Air Quality Kit (PMSA003 + SHT30)](https://shop.m5stack.com/products/pm2-5-air-quality-kit-pmsa003-sht30)
- Optional :
  - [Dlight Unit - Ambient Light Sensor (BH1750FVI-TR)](https://shop.m5stack.com/products/dlight-unit-ambient-light-sensor-bh1750fvi-tr)


## Acknolegment
- [Weather data by Open-Meteo.com](https://open-meteo.com/)
- [Ip geolocation by ipgeolocation.io](https://ipgeolocation.io)