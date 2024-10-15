/*
 * ble.c
 *
 *  Created on: 11. mar. 2024
 *      Author: MadsB
 */



#include <stdio.h>
#include "esp_err.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_blufi.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"



#include "ble.h"


esp_err_t esp_blufi_host_init(void)
{
	int ret;
	esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
	ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
	if (ret) {
		BLE_ERROR("%s init bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
		return ESP_FAIL;
	}

	ret = esp_bluedroid_enable();
	if (ret) {
		BLE_ERROR("%s init bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
		return ESP_FAIL;
	}
	BLE_INFO("BD ADDR: "ESP_BD_ADDR_STR"\n", ESP_BD_ADDR_HEX(esp_bt_dev_get_address()));

	return ESP_OK;

}

esp_err_t esp_blufi_host_deinit(void)
{
	int ret;
	ret = esp_blufi_profile_deinit();
	if(ret != ESP_OK) {
		return ret;
	}

	ret = esp_bluedroid_disable();
	if (ret) {
		BLE_ERROR("%s deinit bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
		return ESP_FAIL;
	}

	ret = esp_bluedroid_deinit();
	if (ret) {
		BLE_ERROR("%s deinit bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
		return ESP_FAIL;
	}

	return ESP_OK;

}

esp_err_t esp_blufi_gap_register_callback(void)
{
	int rc;
	rc = esp_ble_gap_register_callback(esp_blufi_gap_event_handler);
	if(rc){
		return rc;
	}
	return esp_blufi_profile_init();
}

esp_err_t esp_blufi_host_and_cb_init(esp_blufi_callbacks_t *callbacks)
{
	esp_err_t ret = ESP_OK;

	ret = esp_blufi_host_init();
	if (ret) {
		BLE_ERROR("%s initialise host failed: %s\n", __func__, esp_err_to_name(ret));
		return ret;
	}

	ret = esp_blufi_register_callbacks(callbacks);
	if(ret){
		BLE_ERROR("%s blufi register failed, error code = %x\n", __func__, ret);
		return ret;
	}

	ret = esp_blufi_gap_register_callback();
	if(ret){
		BLE_ERROR("%s gap register failed, error code = %x\n", __func__, ret);
		return ret;
	}

	return ESP_OK;

}

esp_err_t esp_blufi_controller_init() {
	esp_err_t ret = ESP_OK;

	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	ret = esp_bt_controller_init(&bt_cfg);
	if (ret) {
		BLE_ERROR("%s initialize bt controller failed: %s\n", __func__, esp_err_to_name(ret));
		return ret;
	}

	ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (ret) {
		BLE_ERROR("%s enable bt controller failed: %s\n", __func__, esp_err_to_name(ret));
		return ret;
	}
	return ret;
}

esp_err_t esp_blufi_controller_deinit() {
	esp_err_t ret = ESP_OK;
	ret = esp_bt_controller_disable();
	if (ret) {
		BLE_ERROR("%s disable bt controller failed: %s\n", __func__, esp_err_to_name(ret));
		return ret;
	}

	ret = esp_bt_controller_deinit();
	if (ret) {
		BLE_ERROR("%s deinit bt controller failed: %s\n", __func__, esp_err_to_name(ret));
		return ret;
	}

	return ret;
}
