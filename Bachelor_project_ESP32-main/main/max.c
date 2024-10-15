/*
 * max.c
 *
 *  Created on: May 2, 2024
 *      Author: metap
 */


#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "max.h"
#include "stdbool.h"
#include "esp_err.h"
#include "string.h"


TaskHandle_t max_reader_task_handle = NULL;

#define I2C_MASTER_NUM I2C_NUM_0
#define MAX17048_SENSOR_ADDR 0x36
#define WRITE_BIT                   I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                    I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                0x1     /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS               0x0     /*!< I2C master will not check ack from slave */
#define ACK_VAL                     0x0     /*!< I2C ack value */
#define NACK_VAL                    0x1     /*!< I2C nack value */
#define SOC_REG                     0x04    /*!< Register address for SoC */
#define CONFIG_REG 0x0C // Example register address
#define MODE_REG 0x06  // Address of the Mode Register
#define TAG_MAX "max17048"

static bool sensor_initialized = false;

volatile double soc = 0.0;

static esp_err_t read_from_max17048(uint8_t reg_addr, uint8_t *data, size_t len) {
	if (data == NULL) {
		return ESP_FAIL;
	}

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, ( MAX17048_SENSOR_ADDR << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, reg_addr, ACK_CHECK_EN);
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (MAX17048_SENSOR_ADDR << 1) | READ_BIT, ACK_CHECK_EN);
	if (len > 1) {
		i2c_master_read(cmd, data, len - 1, ACK_VAL);
	}
	i2c_master_read_byte(cmd, data + len - 1, NACK_VAL);
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	return ret;
}

esp_err_t i2c_write_bytes(i2c_port_t i2c_num, uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len) {
	if (data == NULL) {
		return ESP_FAIL;
	}

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, reg_addr, true);
	i2c_master_write(cmd, data, len, true);
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	return ret;
}

static esp_err_t read_version_number(i2c_port_t i2c_num) {
	uint8_t data[2] = {0};

	esp_err_t ret = read_from_max17048(0x08, data, 2);
	if (ret == ESP_OK) {
		uint16_t version = ((uint16_t)data[0] << 8) | data[1];
		ESP_LOGI(TAG_MAX, "MAX17048 Version Number: 0x%04X", version);
	} else {
		ESP_LOGE(TAG_MAX, "Failed to read version number");
	}
	return ret;
}

esp_err_t enable_quick_start(i2c_port_t i2c_num) {
	uint8_t data[1]; // Buffer to hold data read from the register
	esp_err_t ret;

	// Read the current configuration from the mode register
	ret = read_from_max17048(MODE_REG, data, 1);  // Corrected parameter order
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_MAX, "Failed to read from MODE_REG: %s", esp_err_to_name(ret));
		return ret;
	}

	// Set the QuickStart bit (bit 7) and clear EnSleep (bit 6) and HibStat (bit 5)
	data[0] |= 0x80; // Set QuickStart bit
	data[0] &= ~0x60; // Clear EnSleep and HibStat bits

	// Write back the new configuration
	ret = i2c_write_bytes(i2c_num, MAX17048_SENSOR_ADDR, MODE_REG, data, 1);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_MAX, "Failed to write to MODE_REG: %s", esp_err_to_name(ret));
	}
	return ret;
}


void max_reader_task(void *ignore) {
	uint8_t data[2];

	while (1) {
		// Read voltage from MAX17048
		esp_err_t ret = read_from_max17048(0x02, data, 2);
		if (ret == ESP_OK) {
			uint16_t voltage = ((uint16_t)data[0] << 8) | data[1];
			float voltage_converted = voltage/12000; // conversion to volts v1
			//float voltage_converted = (voltage*1.25f)/1000; // conversion to volts v2
			ESP_LOGI(TAG_MAX, "Battery Voltage: %.2f V", voltage_converted);

		} else {
			ESP_LOGE(TAG_MAX, "Failed to read voltage");
		}

		// Read State of Charge from MAX17048
		ret = read_from_max17048(SOC_REG, data, 2);
		if (ret == ESP_OK) {
			uint16_t raw_soc = ((uint16_t)data[0] << 8) | data[1];
			float state_of_charge = raw_soc * 1.0 / 256.0; // Convert raw SOC to percentage
			ESP_LOGI(TAG_MAX, "Battery SoC: %.2f%%", state_of_charge);
			soc = state_of_charge;
		} else {
			ESP_LOGE(TAG_MAX, "Failed to read SoC");
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	vTaskDelete(NULL);
}

void max_main(void) {
	esp_err_t err;
	enable_quick_start(I2C_MASTER_NUM);
	err = read_version_number(I2C_MASTER_NUM);
	if (err != ESP_OK) {
		ESP_LOGE(TAG_MAX, "Failed to read version number");
	}

	if(!sensor_initialized){
		xTaskCreate(max_reader_task, "max_reader_task", 4096, NULL, 5, &max_reader_task_handle);
		sensor_initialized=true;
	}

}

void stop_max(void){
	if (max_reader_task_handle != NULL) {
		vTaskDelete(max_reader_task_handle);
		max_reader_task_handle = NULL; // Reset the task handle
		sensor_initialized = false; // Reset the initialization flag
		ESP_LOGE(TAG_MAX, "max_readertask stopped! \n");
	}
}
