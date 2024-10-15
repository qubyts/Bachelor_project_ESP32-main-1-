/*
 * http_func.c
 *
 *  Created on: 3. mai 2024
 *      Author: MadsB
 */
#include <driver/i2c.h>
#include <esp_err.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "wifi.h"

#define SERVER_URL_FORMAT "http://%s"
#define SERVER_URL_BUFFER_SIZE (strlen(SERVER_URL_FORMAT) + 256)

#define TAG_HTTP "HTTP_POST"

esp_err_t send_data_http(char *device_name, double temperature, double humidity, double charge){
    char SERVER_URL[SERVER_URL_BUFFER_SIZE];
    sprintf(SERVER_URL, SERVER_URL_FORMAT, uri);

    esp_http_client_config_t config = {
            .url = SERVER_URL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG_HTTP, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    char post_data[100];
    snprintf(post_data, sizeof(post_data), "device_name=%s&temperature=%.2f&humidity=%.3f&charge=%.2f", device_name, temperature, humidity, charge);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    // If we reach this point, the request was successful
    ESP_LOGI(TAG_HTTP, "HTTP post successful");

    // Cleanup the client handle
    esp_http_client_cleanup(client);

    return ESP_OK;
}
