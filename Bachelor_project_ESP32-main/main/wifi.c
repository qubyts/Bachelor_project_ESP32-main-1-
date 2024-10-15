#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_blufi_api.h"
#include "ble.h"


#include "esp_blufi.h"

#define WIFI_CONNECTION_MAXIMUM_RETRY 9
#define INVALID_REASON                255
#define INVALID_RSSI                  -128


#define ESP_BLUFI_CUSTOM_DATA_MAX_LEN 256 // Maximum length of custom data
#define MAX_IP_LENGTH 256 // Maximum length of an IP address (including null terminator)
#define MAX_TIMER_LENGTH 16 // Maximum length of the timer (including null terminator)


char name[ESP_BLUFI_CUSTOM_DATA_MAX_LEN + 1];
char uri[MAX_IP_LENGTH];
char timer[MAX_TIMER_LENGTH];


void event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

#define WIFI_LIST_NUM   10

wifi_config_t sta_config;
wifi_config_t ap_config;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

uint8_t wifi_retry = 0;

/* store the station info for send back to phone */
bool gl_sta_connected = false;
bool gl_sta_got_ip = false;
bool ble_is_connected = false;
uint8_t gl_sta_bssid[6];
uint8_t gl_sta_ssid[32];
int gl_sta_ssid_len;
wifi_sta_list_t gl_sta_list;
bool gl_sta_is_connecting = false;
esp_blufi_extra_info_t gl_sta_conn_info;


esp_err_t save_custom_data_to_nvs(const char* key, const char* value) {
	nvs_handle_t nvs_handle;
	esp_err_t err = nvs_open("custom_storage", NVS_READWRITE, &nvs_handle);
	if (err != ESP_OK) {
		return err;
	}

	err = nvs_set_str(nvs_handle, key, value);
	if (err != ESP_OK) {
		nvs_close(nvs_handle);
		return err;
	}

	err = nvs_commit(nvs_handle);
	nvs_close(nvs_handle);
	return err;
}


void record_wifi_conn_info(int rssi, uint8_t reason)
{
	memset(&gl_sta_conn_info, 0, sizeof(esp_blufi_extra_info_t));
	if (gl_sta_is_connecting) {
		gl_sta_conn_info.sta_max_conn_retry_set = true;
		gl_sta_conn_info.sta_max_conn_retry = WIFI_CONNECTION_MAXIMUM_RETRY;
	} else {
		gl_sta_conn_info.sta_conn_rssi_set = true;
		gl_sta_conn_info.sta_conn_rssi = rssi;
		gl_sta_conn_info.sta_conn_end_reason_set = true;
		gl_sta_conn_info.sta_conn_end_reason = reason;
	}
}

void wifi_connect(void)
{
	wifi_retry = 0;
	gl_sta_is_connecting = (esp_wifi_connect() == ESP_OK);
	record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
}

bool wifi_reconnect(void)
{
	bool ret;
	if (gl_sta_is_connecting && wifi_retry++ < WIFI_CONNECTION_MAXIMUM_RETRY) {
		BLE_INFO("BLUFI WiFi starts reconnection\n");
		gl_sta_is_connecting = (esp_wifi_connect() == ESP_OK);
		record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
		ret = true;
	} else {
		BLE_INFO("BLUFI WiFi Connection Failed after: %i attempts \n", wifi_retry);
		ret = false;
	}
	return ret;
}


int softap_get_current_connection_number(void)
{
	esp_err_t ret;
	ret = esp_wifi_ap_get_sta_list(&gl_sta_list);
	if (ret == ESP_OK)
	{
		return gl_sta_list.num;
	}

	return 0;
}

void ip_event_handler(void* arg, esp_event_base_t event_base,
		int32_t event_id, void* event_data)
{
	wifi_mode_t mode;

	switch (event_id) {
	case IP_EVENT_STA_GOT_IP: {
		esp_blufi_extra_info_t info;

		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		esp_wifi_get_mode(&mode);

		memset(&info, 0, sizeof(esp_blufi_extra_info_t));
		memcpy(info.sta_bssid, gl_sta_bssid, 6);
		info.sta_bssid_set = true;
		info.sta_ssid = gl_sta_ssid;
		info.sta_ssid_len = gl_sta_ssid_len;
		gl_sta_got_ip = true;
		if (ble_is_connected == true) {
			esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, softap_get_current_connection_number(), &info);
		} else {
			BLE_INFO("BLUFI BLE is not connected yet\n");
		}
		break;
	}
	default:
		break;
	}
	return;
}

void wifi_event_handler(void* arg, esp_event_base_t event_base,
		int32_t event_id, void* event_data)
{
	wifi_event_sta_connected_t *event;
	wifi_event_sta_disconnected_t *disconnected_event;
	wifi_mode_t mode;

	switch (event_id) {
	case WIFI_EVENT_STA_START:
		wifi_connect();
		break;
	case WIFI_EVENT_STA_CONNECTED:
		gl_sta_connected = true;
		gl_sta_is_connecting = false;
		event = (wifi_event_sta_connected_t*) event_data;
		memcpy(gl_sta_bssid, event->bssid, 6);
		memcpy(gl_sta_ssid, event->ssid, event->ssid_len);
		gl_sta_ssid_len = event->ssid_len;
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		/* Only handle reconnection during connecting */
		if (gl_sta_connected == false && wifi_reconnect() == false) {
			gl_sta_is_connecting = false;
			disconnected_event = (wifi_event_sta_disconnected_t*) event_data;
			record_wifi_conn_info(disconnected_event->rssi, disconnected_event->reason);
		}
		/* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
		gl_sta_connected = false;
		gl_sta_got_ip = false;
		memset(gl_sta_ssid, 0, 32);
		memset(gl_sta_bssid, 0, 6);
		gl_sta_ssid_len = 0;
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		break;
	case WIFI_EVENT_AP_START:
		esp_wifi_get_mode(&mode);

		/* TODO: get config or information of softap, then set to report extra_info */
		if (ble_is_connected == true) {
			if (gl_sta_connected) {
				esp_blufi_extra_info_t info;
				memset(&info, 0, sizeof(esp_blufi_extra_info_t));
				memcpy(info.sta_bssid, gl_sta_bssid, 6);
				info.sta_bssid_set = true;
				info.sta_ssid = gl_sta_ssid;
				info.sta_ssid_len = gl_sta_ssid_len;
				esp_blufi_send_wifi_conn_report(mode, gl_sta_got_ip ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP, softap_get_current_connection_number(), &info);
			} else if (gl_sta_is_connecting) {
				esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, softap_get_current_connection_number(), &gl_sta_conn_info);
			} else {
				esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, softap_get_current_connection_number(), &gl_sta_conn_info);
			}
		} else {
			BLE_INFO("BLUFI BLE is not connected yet\n");
		}
		break;
	case WIFI_EVENT_SCAN_DONE: {
		uint16_t apCount = 0;
		esp_wifi_scan_get_ap_num(&apCount);
		if (apCount == 0) {
			BLE_INFO("Nothing AP found");
			break;
		}
		wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
		if (!ap_list) {
			BLE_ERROR("malloc error, ap_list is NULL");
			break;
		}
		ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
		esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
		if (!blufi_ap_list) {
			if (ap_list) {
				free(ap_list);
			}
			BLE_ERROR("malloc error, blufi_ap_list is NULL");
			break;
		}
		for (int i = 0; i < apCount; ++i)
		{
			blufi_ap_list[i].rssi = ap_list[i].rssi;
			memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
		}

		if (ble_is_connected == true) {
			esp_blufi_send_wifi_list(apCount, blufi_ap_list);
		} else {
			BLE_INFO("BLUFI BLE is not connected yet\n");
		}

		esp_wifi_scan_stop();
		free(ap_list);
		free(blufi_ap_list);
		break;
	}
	case WIFI_EVENT_AP_STACONNECTED: {
		wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
		BLE_INFO("station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
		break;
	}
	case WIFI_EVENT_AP_STADISCONNECTED: {
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		BLE_INFO("station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
		break;
	}

	default:
		break;
	}
	return;
}

void initialise_wifi(void)
{
	ESP_ERROR_CHECK(esp_netif_init());
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
	assert(sta_netif);
	esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
	assert(ap_netif);
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
	ESP_ERROR_CHECK( esp_wifi_start() );

	nvs_handle_t nvs_handle;
	esp_err_t err = nvs_open("custom_storage", NVS_READONLY, &nvs_handle);
	if (err == ESP_OK) {
		size_t required_size;

		// Read custom name data
		err = nvs_get_str(nvs_handle, "name", NULL, &required_size);
		if (err == ESP_OK) {
			char *name_buffer = malloc(required_size);
			if (name_buffer) {
				err = nvs_get_str(nvs_handle, "name", name_buffer, &required_size);
				if (err == ESP_OK) {
					strncpy(name, name_buffer, ESP_BLUFI_CUSTOM_DATA_MAX_LEN);
					name[ESP_BLUFI_CUSTOM_DATA_MAX_LEN] = '\0'; // Ensure null-termination
				}
				free(name_buffer);
			}
		}

		// Read custom IP data
		err = nvs_get_str(nvs_handle, "uri", NULL, &required_size);
		if (err == ESP_OK) {
			char *uri_buffer = malloc(required_size);
			if (uri_buffer) {
				err = nvs_get_str(nvs_handle, "uri", uri_buffer, &required_size);
				if (err == ESP_OK) {
					strncpy(uri, uri_buffer, MAX_IP_LENGTH - 1); // Copy IP address string
					uri[MAX_IP_LENGTH - 1] = '\0'; // Ensure null-termination
				}
				free(uri_buffer);
			}
		}

		// Read custom timer data
		err = nvs_get_str(nvs_handle, "timer", NULL, &required_size);
		if (err == ESP_OK) {
			char *timer_buffer = malloc(required_size);
			if (timer_buffer) {
				err = nvs_get_str(nvs_handle, "timer", timer_buffer, &required_size);
				if (err == ESP_OK) {
					strncpy(timer, timer_buffer, MAX_TIMER_LENGTH - 1); // Copy timer string
					timer[MAX_TIMER_LENGTH - 1] = '\0'; // Ensure null-termination
				}
				free(timer_buffer);
			}
		}

		nvs_close(nvs_handle);
	}
}


esp_blufi_callbacks_t callbacks = {
		.event_cb = event_callback,
		.negotiate_data_handler = blufi_dh_negotiate_data_handler,
		.encrypt_func = blufi_aes_encrypt,
		.decrypt_func = blufi_aes_decrypt,
		.checksum_func = blufi_crc_checksum,
};

void event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
	/* actually, should post to blufi_task handle the procedure,
	 * now, as a example, we do it more simply */
	switch (event) {


	case ESP_BLUFI_EVENT_INIT_FINISH:
		BLE_INFO("BLUFI init finish\n");

		esp_blufi_adv_start();
		break;


	case ESP_BLUFI_EVENT_DEINIT_FINISH:
		BLE_INFO("BLUFI deinit finish\n");
		break;


	case ESP_BLUFI_EVENT_BLE_CONNECT:
		BLE_INFO("BLUFI ble connect\n");
		ble_is_connected = true;
		esp_blufi_adv_stop();
		blufi_security_init();
		break;


	case ESP_BLUFI_EVENT_BLE_DISCONNECT:
		BLE_INFO("BLUFI ble disconnect\n");
		ble_is_connected = false;
		blufi_security_deinit();
		esp_blufi_adv_start();
		break;


	case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
		BLE_INFO("BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
		ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
		break;


	case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
		BLE_INFO("BLUFI requset wifi connect to AP\n");
		/* there is no wifi callback when the device has already connected to this wifi
        so disconnect wifi before connection.
		 */
		esp_wifi_disconnect();
		wifi_connect();
		break;


	case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
		BLE_INFO("BLUFI requset wifi disconnect from AP\n");
		esp_wifi_disconnect();
		break;


	case ESP_BLUFI_EVENT_REPORT_ERROR:
		BLE_ERROR("BLUFI report error, error code %d\n", param->report_error.state);
		esp_blufi_send_error_info(param->report_error.state);
		break;


	case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
		wifi_mode_t mode;
		esp_blufi_extra_info_t info;

		esp_wifi_get_mode(&mode);

		if (gl_sta_connected) {
			memset(&info, 0, sizeof(esp_blufi_extra_info_t));
			memcpy(info.sta_bssid, gl_sta_bssid, 6);
			info.sta_bssid_set = true;
			info.sta_ssid = gl_sta_ssid;
			info.sta_ssid_len = gl_sta_ssid_len;
			esp_blufi_send_wifi_conn_report(mode, gl_sta_got_ip ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP, softap_get_current_connection_number(), &info);
		} else if (gl_sta_is_connecting) {
			esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, softap_get_current_connection_number(), &gl_sta_conn_info);
		} else {
			esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, softap_get_current_connection_number(), &gl_sta_conn_info);
		}
		BLE_INFO("BLUFI get wifi status from AP\n");

		break;
	}


	case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
		BLE_INFO("blufi close a gatt connection");
		esp_blufi_disconnect();
		break;


	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
		memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
		sta_config.sta.bssid_set = 1;
		esp_wifi_set_config(WIFI_IF_STA, &sta_config);
		BLE_INFO("Recv STA BSSID %s\n", sta_config.sta.ssid);
		break;


	case ESP_BLUFI_EVENT_RECV_STA_SSID:
		strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
		sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
		esp_wifi_set_config(WIFI_IF_STA, &sta_config);
		BLE_INFO("Recv STA SSID %s\n", sta_config.sta.ssid);
		break;


	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
		strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
		sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
		esp_wifi_set_config(WIFI_IF_STA, &sta_config);
		BLE_INFO("Recv STA PASSWORD %s\n", sta_config.sta.password);
		break;


		// Custom set up for device. Name of IoT node, IP/URL for server and Deep sleep timer in seconds.
	case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA: {
		// Ensure the custom data length does not exceed the buffer size
		uint16_t data_len = param->custom_data.data_len;
		if (data_len > ESP_BLUFI_CUSTOM_DATA_MAX_LEN) {
			data_len = ESP_BLUFI_CUSTOM_DATA_MAX_LEN;
		}

		// Copy the custom data to the buffer
		char data_buffer[ESP_BLUFI_CUSTOM_DATA_MAX_LEN + 1];
		memcpy(data_buffer, param->custom_data.data, data_len);
		data_buffer[data_len] = '\0'; // Null terminate the string

		//Name Data
		if (strncmp(data_buffer, "name:", 5) == 0) {
			const char* name = &data_buffer[5]; // Skip "name:"
			printf("Received name: %s \n", name);
			esp_err_t nvs_err = save_custom_data_to_nvs("name", name);
			if (nvs_err != ESP_OK) {
				printf("Error saving name to NVS: %s\n", esp_err_to_name(nvs_err));
			}

			//IP data
		} else if (strncmp(data_buffer, "uri:", 4) == 0) {
			const char* uri = &data_buffer[4]; // Skip "ip:"
			printf("Received URI address: %s \n", uri);
			esp_err_t nvs_err = save_custom_data_to_nvs("uri", uri);
			if (nvs_err != ESP_OK) {
				printf("Error saving URI address to NVS: %s\n", esp_err_to_name(nvs_err));
			}

			//Timer Data
		} else if (strncmp(data_buffer, "timer:", 6) == 0) {
			const char* timer = &data_buffer[6]; // Skip "timer:"
			printf("Received timer: %s \n", timer);
			esp_err_t nvs_err = save_custom_data_to_nvs("timer", timer);
			if (nvs_err != ESP_OK) {
				printf("Error saving timer to NVS: %s\n", esp_err_to_name(nvs_err));
			}
		} else {
			// if not a recognized prefix
			printf("Unknown custom data format: %s \n", data_buffer);
		}

		break;
	}

	case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
		wifi_scan_config_t scanConf = {
				.ssid = NULL,
				.bssid = NULL,
				.channel = 0,
				.show_hidden = false
		};
		esp_err_t ret = esp_wifi_scan_start(&scanConf, true);
		if (ret != ESP_OK) {
			esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
		}
		break;
	}
	default:
		break;
	}
}

void blufi_func(void)
{
	esp_err_t ret;

	ret = esp_blufi_controller_init();
	if (ret) {
		BLE_ERROR("%s BLUFI controller init failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	ret = esp_blufi_host_and_cb_init(&callbacks);
	if (ret) {
		BLE_ERROR("%s initialise failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	BLE_INFO("BLUFI VERSION %04x\n", esp_blufi_get_version());
}



void ble_deinit(void)
{
	esp_err_t ret;

	ret = esp_blufi_host_deinit();
	if (ret){
		BLE_ERROR("%s BLUFI host deinit failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}

	vTaskDelay(10/portTICK_PERIOD_MS);

	ret = esp_blufi_controller_deinit();
	if (ret) {
		BLE_ERROR("%s BLUFI controller deinit failed: %s\n", __func__, esp_err_to_name(ret));
		return;
	}


	BLE_INFO("BLUFI deinit");

}

void wifi_on(void){
	esp_err_t ret;
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	initialise_wifi();


}

bool is_wifi_connected(void) {
	wifi_ap_record_t ap_info;
	esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
	return (ret == ESP_OK);
}

