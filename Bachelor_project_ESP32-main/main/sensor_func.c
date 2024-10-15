/*
 * sensor_func.c
 *
 *  Created on: 2 Feb 2024
 *      Author: metap
 */
#include <bme280.h>
#include <driver/i2c.h>
#include <esp_err.h>
#include <hal/gpio_types.h>
#include <hal/i2c_types.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <freertos/event_groups.h>
#include <freertos/portmacro.h>
#include <freertos/projdefs.h>
#include <freertos/task.h>
#include "sensor_func.h"
#include "esp_log.h"
#include "esp_http_client.h"


#define TAG_BME280 "BME280"
volatile double temp = 0.0;
volatile double hum = 0.0;
volatile double press = 0.0;

TaskHandle_t bme280_reader_task_handle = NULL;

static bool sensor_initialized = false; // Flag to track initialization


s8 BME280_I2C_bus_write(u8 dev_addr, u8 reg_addr, u8 *reg_data, u8 cnt)
{
	s32 iError = BME280_INIT_VALUE;

	esp_err_t espRc;
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);

	i2c_master_write_byte(cmd, reg_addr, true);
	i2c_master_write(cmd, reg_data, cnt, true);
	i2c_master_stop(cmd);

	espRc = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10/portTICK_PERIOD_MS);

	if (espRc == ESP_OK) {
		iError = SUCCESS;
	} else {
		iError = ERROR;
	}

	i2c_cmd_link_delete(cmd);

	return (s8)iError;
}

s8 BME280_I2C_bus_read(u8 dev_addr, u8 reg_addr, u8 *reg_data, u8 cnt)
{
	s32 iError = BME280_INIT_VALUE;
	esp_err_t espRc;

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, reg_addr, true);

	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);

	if (cnt > 1) {
		i2c_master_read(cmd, reg_data, cnt-1, I2C_MASTER_ACK);
	}
	i2c_master_read_byte(cmd, reg_data+cnt-1, I2C_MASTER_NACK);
	i2c_master_stop(cmd);

	espRc = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10/portTICK_PERIOD_MS);

	if (espRc == ESP_OK) {
		iError = SUCCESS;
	} else {
		iError = ERROR;
	}


	i2c_cmd_link_delete(cmd);

	return (s8)iError;
}

void BME280_delay_msek(u32 msek)
{
	vTaskDelay(msek/portTICK_PERIOD_MS);
}


void bme280_reader_task(void *ignore)
{


	struct bme280_t bme280 = {
			.bus_write = BME280_I2C_bus_write,
			.bus_read = BME280_I2C_bus_read,
			.dev_addr = BME280_I2C_ADDRESS1,
			.delay_msec = BME280_delay_msek
	};

	s32 com_rslt;
	s32 v_uncomp_pressure_s32;
	s32 v_uncomp_temperature_s32;
	s32 v_uncomp_humidity_s32;

	com_rslt = bme280_init(&bme280);

	com_rslt += bme280_set_oversamp_pressure(BME280_OVERSAMP_16X);
	com_rslt += bme280_set_oversamp_temperature(BME280_OVERSAMP_2X);
	com_rslt += bme280_set_oversamp_humidity(BME280_OVERSAMP_1X);

	com_rslt += bme280_set_standby_durn(BME280_STANDBY_TIME_1_MS);
	com_rslt += bme280_set_filter(BME280_FILTER_COEFF_16);

	com_rslt += bme280_set_power_mode(BME280_NORMAL_MODE);
	if (com_rslt == SUCCESS) {
		while(true) {

			vTaskDelay(400 / portTICK_PERIOD_MS);

			com_rslt = bme280_read_uncomp_pressure_temperature_humidity(
					&v_uncomp_pressure_s32, &v_uncomp_temperature_s32, &v_uncomp_humidity_s32);

			if (com_rslt == SUCCESS) {
				double temp_comp = bme280_compensate_temperature_double(v_uncomp_temperature_s32);
				double press_comp = bme280_compensate_pressure_double(v_uncomp_pressure_s32) / 100;
				double hum_comp = bme280_compensate_humidity_double(v_uncomp_humidity_s32);

				ESP_LOGI(TAG_BME280, "%.2f degC / %.3f hPa / %.3f %%",
						temp_comp, press_comp, hum_comp);

				// Update variables
				temp = temp_comp;
				press = press_comp;
				hum = hum_comp;


				vTaskDelay(100 / portTICK_PERIOD_MS);

			} else {
				ESP_LOGE(TAG_BME280, "measure error. code: %d", com_rslt);
			}
		}
	} else {
		ESP_LOGE(TAG_BME280, "init or setting error. code: %d", com_rslt);
	}

	vTaskDelete(NULL);
}




void bme280_sensor_func(void){
	if (!sensor_initialized) {
		xTaskCreate(&bme280_reader_task, "bme280_reader_task", 4096, NULL, 6, &bme280_reader_task_handle);
		sensor_initialized = true;

	}
}



void stop_bme280(void){
	if (bme280_reader_task_handle != NULL) {
		vTaskDelete(bme280_reader_task_handle);
		bme280_reader_task_handle = NULL; // Reset the task handle
		sensor_initialized = false; // Reset the initialization flag
		ESP_LOGE(TAG_BME280, "BME280_reader task stopped! \n");
	}
}

