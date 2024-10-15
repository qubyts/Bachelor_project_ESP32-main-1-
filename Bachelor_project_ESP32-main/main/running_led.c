#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define RUNNING_LED 4

TaskHandle_t run_led_task_handle = NULL; // Task handle for the LED blinking task

void run_led_init(void){
    gpio_reset_pin(RUNNING_LED);
    gpio_set_direction(RUNNING_LED, GPIO_MODE_OUTPUT);
}

void run_led_task(void *pvParameters) {
    int delay_ms = *((int *)pvParameters);
    while (1) {
        gpio_set_level(RUNNING_LED, 1); // Turn on the LED
        vTaskDelay(delay_ms / portTICK_PERIOD_MS); // Delay for the specified time in milliseconds
        gpio_set_level(RUNNING_LED, 0); // Turn off the LED
        vTaskDelay(delay_ms / portTICK_PERIOD_MS); // Delay for the specified time again
    }
}

void start_led_task(int delay_ms) {
    if (run_led_task_handle == NULL) {
        // Create the LED task if it hasn't been created yet
        xTaskCreate(run_led_task, "run_led_task", 2048, &delay_ms, configMAX_PRIORITIES - 1, &run_led_task_handle);
    }
    else {
        // Update the delay time if the task is already running
        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay for safety
        vTaskSuspend(run_led_task_handle); // Suspend the task
        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay for safety
        vTaskDelete(run_led_task_handle); // Delete the task
        xTaskCreate(run_led_task, "run_led_task", 2048, &delay_ms, configMAX_PRIORITIES - 1, &run_led_task_handle); // Create a new task with the updated delay time
    }
}

void stop_led_task() {
    if (run_led_task_handle != NULL) {
        // Delete the LED task if it's running
        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay for safety
        vTaskSuspend(run_led_task_handle); // Suspend the task
        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay for safety
        vTaskDelete(run_led_task_handle); // Delete the task
        run_led_task_handle = NULL; // Reset the task handle
    }
}

