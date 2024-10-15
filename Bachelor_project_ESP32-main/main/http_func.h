/*
 * http_func.h
 *
 *  Created on: 3. mai 2024
 *      Author: MadsB
 */

#ifndef MAIN_HTTP_FUNC_H_
#define MAIN_HTTP_FUNC_H_



esp_err_t  send_data_http(char *device_name, double temperature, double humidity, double charge);

#endif /* MAIN_HTTP_FUNC_H_ */
