# Bachelor_project_ESP32-main
 
# ESP32-C3 WiFi Provisioning via BLE

This project enables WiFi provisioning on an ESP32-C3 chip using Bluetooth Low Energy (BLE) through the BLUFI protocol. Additionally, it sets up GPIO pins for interfacing with I2C devices, including a BME280 sensor and a MAX17048 fuel gauge.

## Overview

The project allows seamless provisioning of WiFi credentials and other configuration settings to the ESP32-C3 chip via BLE, eliminating the need for manual configuration. It also facilitates data transmission from sensors to a web server while providing the flexibility to configure various parameters remotely.

## Features

- WiFi provisioning via BLE using the BLUFI protocol.
- Interfacing with I2C devices (BME280 sensor and MAX17048 Battery fuel gauge).
- Two operation modes: normal running mode (WiFi mode) and configuration mode (BLE mode).
- Configuration of WiFi credentials, device name, web server URL, and deep sleep timer via BLE.

## Usage

To run the program, follow these steps:

1. Create a new Espressif project, use template sample project.
2. Copy the project files, ESP libraries, and configuration files to your development environment.
3. Ensure the device is set into upload mode if you are not using a devkit board.
4. Use device manager to ensure bauderate for the device is set to 115200.
5. Compile and upload the firmware to your ESP32-C3 chip using the Espressif IDE.
6. If this is the first time you are installing software to the chip upload twice, once for bootloader and once for the application
7. Power on the ESP32-C3 chip.
8. First time boot the device will not have a name, wifi credentials a deepsleep timer or URI for webserver. use an appropriate app to send the configurations.
9. If you are using ESPBluFi to send WiFi credentials use configure and send WiFi ssid and password to the device, then use input custom text with prefix "name:", "uri:", "timer:" to define the name of the device the URI for the webserver and the sleep timer in minutes.
10. After the device has the configuration press the reset button. the device will save everything to non-volatile storage.
11. If you want to change the advertised name of the device for Bluetooth purposes open esp_blufi.h and edit BLUFI_DEVICE_NAME
12. Note: our App uses BLUFI as a prefix parameter if you remove this part the device will not be found in the app.

### Configuration Files

Ensure you copy the following configuration files:
- **nvs.csv:** NVS partition layout file.
- **partitions.csv:** Partition table layout file.
- **sdkconfig:** SDK configuration file.

## Operation Modes

- **WiFi mode:** In this mode, the ESP32-C3 chip sends data gathered from sensors to a web server.
- **BLE mode:** Use a BLE-capable device to scan for and connect to the ESP32-C3 device. Follow the BLE prompts to configure WiFi credentials, device name, web server URL, and deep sleep timer.

## GPIO Pin Configuration

- **SDA:** I2C data line.
- **SCL:** I2C clock line.
- **BLE_BUTTON:** GPIO pin for switching between operation modes.
- **WAKEUP_BUTTOM:** GPIO pin with button has not been used for anything but is on our pcb and future development for the button is in mind.


