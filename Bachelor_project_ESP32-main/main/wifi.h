/*
 * wifi.h
 *
 *  Created on: 12. mar. 2024
 *      Author: MadsB
 */

#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_


#define ESP_BLUFI_CUSTOM_DATA_MAX_LEN 256 // Maximum length of custom data
#define MAX_IP_LENGTH 256 // Maximum length of an IP address
#define MAX_TIMER_LENGTH 16 // Maximum length of the timer

#define WIFI_CONNECTION_MAXIMUM_RETRY 9
extern uint8_t wifi_retry;

extern char name[ESP_BLUFI_CUSTOM_DATA_MAX_LEN + 1];
extern char timer[MAX_TIMER_LENGTH];
extern char uri[MAX_IP_LENGTH];


void blufi_func(void);
void ble_deinit(void);
void wifi_on(void);
bool is_wifi_connected(void);
void wifi_connect(void);
bool wifi_reconnect(void);
#endif /* MAIN_WIFI_H_ */
