//libraries provided by Espressif IDE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"

#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "hal/i2c_types.h"

#include "esp_timer.h"
#include "esp_blufi_api.h"
#include "esp_blufi.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

//Config of the ESP32-c3 chip
#include "sdkconfig.h"

//private includes
#include "ble.h"							// Header file for the BLE and BluFi functions
#include "wifi.h"							// Header file for the BluFi and wifi functions
#include "sensor_func.h"					// Header file for the BME280 measurment function
#include "bme280.h" 						// Header file for the BME280 component library
#include "http_func.h" 						// Header file for the HTTP post function
#include "running_led.h" 					// Header file for the running led thread
#include "max.h" 							// Header file for the MAX17048 sensor



//GPIO defines
#define BLE_BUTTON GPIO_NUM_10
#define SDA_PIN GPIO_NUM_6
#define SCL_PIN GPIO_NUM_7

//this button is on the board but has not been configured for anything.
#define WAKEUP_BUTTON GPIO_NUM_0

//TAG for log messages, this is the easiest way to debug with ESPRESSIF IDE.
#define MAIN_TAG "MAIN_TAG"

//Sleep timer defines
#define DEEP_SLEEP_CONVERT 1000000
#define TIMEOUTPERIOD 20000 					// equeal to 20 seconds


//adjust as needed, the BME280 sensor will also get some temprature data from its own heat and the heat of the PCB
#define TEMPCALIBRATION 3

uint32_t DEEP_SLEEP_PERIOD;


//Boolean variables
volatile bool button_pressed = false; 			// to prevent changing states when application starts
volatile bool switch_case = false; 				// false for WiFi, true for BLE
volatile bool should_enter_deep_sleep = true; 	//if false wont enter deep sleep
volatile bool timeout = false;					// if false the device will not have a time out  when in BLE mode

//handle for switching between BLE mode and WiFi mode
TaskHandle_t switch_mode_task_handle;

void switch_mode() {
	switch_case = !switch_case; 				// Toggle the mode

	if (switch_case) {
		should_enter_deep_sleep = false;
		stop_bme280();
		stop_max();
		blufi_func();
		vTaskDelay(10 / portTICK_PERIOD_MS); 	//small delay to ensure Blufi get enabled
		printf("Switched to BLE mode\n");
		stop_led_task();
		vTaskDelay(100/portTICK_PERIOD_MS);		// stop, delay and start to ensure the running led thread isnt doubly initialized.
		start_led_task(100);

	} else {

		should_enter_deep_sleep = true;
		ble_deinit();							// Disable Bluetooth to save power
		vTaskDelay(10 / portTICK_PERIOD_MS); 	// small delay to ensure blufi gets disabled

		//check for internet connection
		if (is_wifi_connected()) {
			ESP_LOGI("WiFi", "ESP32 is connected to WiFi");
		} else {
			ESP_LOGE("WiFi", "ESP32 is not connected to WiFi");
		}
		printf("Switched to WiFi mode\n");

		stop_led_task();
		vTaskDelay(100/portTICK_PERIOD_MS); 	// stop, delay and start to ensure the running led thread isnt doubly initialized.
		start_led_task(100);



	}
}

void switch_mode_task(void *pvParameters) {
	while(1) {
		if (button_pressed) {
			switch_mode(); 						// Handle mode switching
			button_pressed = false; 			// Reset button flag
		}
		vTaskDelay(100 / portTICK_PERIOD_MS); 	//delay to prevent de-bounce
	}
	vTaskDelete(NULL);
}


//callback for button interrupt
void button_callback(void* arg) {
	button_pressed = true;
}

//i2c master bus init
void i2c_master_init(void)
{
	i2c_config_t i2c_config = {
			.mode = I2C_MODE_MASTER,
			.sda_io_num = SDA_PIN,
			.scl_io_num = SCL_PIN,
			.sda_pullup_en = GPIO_PULLUP_ENABLE,
			.scl_pullup_en = GPIO_PULLUP_ENABLE,
			.master.clk_speed = 1000000
	};
	i2c_param_config(I2C_NUM_0, &i2c_config);
	i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

//main application
void app_main(void) {
	//create button thread for changing which mode we are operating in
	xTaskCreate(&switch_mode_task, "Switch Mode Task", 4096, NULL, configMAX_PRIORITIES - 1, &switch_mode_task_handle);

	run_led_init();

	i2c_master_init();

	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
	io_conf.pin_bit_mask = (1ULL << BLE_BUTTON);
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config(&io_conf);
	gpio_install_isr_service(0);
	gpio_isr_handler_add(BLE_BUTTON, button_callback, NULL);

	//init for Wifi
	wifi_on();

	vTaskDelay(100 / portTICK_PERIOD_MS);



	//check if wifi is connected
	if (is_wifi_connected()) {
		ESP_LOGI("WiFi", "ESP32 is connected to WiFi");
	} else{
		//enter BLE mode if not connected
		ESP_LOGI("WiFi", "ESP32 is not connected to WiFi");
		ESP_LOGI("WiFi", "Device might not have correct WiFi Credentials \n");
		timeout = true;
		button_pressed = true;
	}

	printf("You have 5 seconds to cancel deepsleep\n");
	vTaskDelay(5000 / portTICK_PERIOD_MS);


	while (1) {
		if (switch_case) {
			// BLE mode
			vTaskDelay(100 / portTICK_PERIOD_MS);
			int timeout_counter = 0;
			if (timeout==true){
				while (timeout_counter < TIMEOUTPERIOD) {
					vTaskDelay( TIMEOUTPERIOD / portTICK_PERIOD_MS);
					timeout_counter = TIMEOUTPERIOD;
					vTaskDelay(10/portTICK_PERIOD_MS);
					break;
				}

				button_pressed = true; // Trigger mode switch as if button was pressed

				vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay after mode switch
			}
		} else {
			// WiFi mode
			if (should_enter_deep_sleep==true) {

				//blink running led twice a second to indicate wifi mode, function found in running_sensor.c
				start_led_task(500);



				//temp and hum measurment found in bme280.c
				bme280_sensor_func();

				//battery monitor found in max.c
				max_main();

				//delay to ensure sensors are able to measure before moving on
				vTaskDelay(500 / portTICK_PERIOD_MS);

				//send name of device, temperature, humidity and state of charge to our webserver function found in http_func.c
				send_data_http(name, temp-TEMPCALIBRATION, hum, soc);

				//LOG message for what is sendt to the server
				ESP_LOGI(MAIN_TAG, "%s / %.2f / %.3f / %.2f", name, temp-TEMPCALIBRATION, hum, soc);

				//LOG message for how the device is configured
				ESP_LOGI(MAIN_TAG, "name is:%s / uri:%s / timer for deepsleep is:%s", name, uri, timer);

				int timer_value = atoi(timer);

				DEEP_SLEEP_PERIOD = (timer_value*60) * DEEP_SLEEP_CONVERT;

				printf("Entering deep sleep for %.0f minutes\n", ((float)DEEP_SLEEP_PERIOD/60) / DEEP_SLEEP_CONVERT);

				vTaskDelay(10/portTICK_PERIOD_MS);

				//setup for deep sleep period
				esp_sleep_enable_timer_wakeup(DEEP_SLEEP_PERIOD);

				// Enter deep sleep mode
				esp_deep_sleep_start();

			}
		}
	}
}



