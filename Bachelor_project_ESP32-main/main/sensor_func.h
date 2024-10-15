#ifndef SENSOR_FUNC_H_
#define SENSOR_FUNC_H_

extern volatile double hum;
extern volatile double temp;

#define my_device_name

void bme280_sensor_func(void);
void stop_bme280(void);





#endif /* SENSOR_FUNC_H_ */
