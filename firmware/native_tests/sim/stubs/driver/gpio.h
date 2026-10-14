// Stand-in for the ESP-IDF GPIO driver (PC simulation only).
#pragma once

#include <cstdint>

#include "esp_err.h"
#define ESP_INTR_FLAG_IRAM (1 << 10)

typedef int gpio_num_t;
typedef enum { GPIO_INTR_DISABLE = 0, GPIO_INTR_ANYEDGE = 3 } gpio_int_type_t;
typedef void (*gpio_isr_t)(void*);
typedef enum { GPIO_DRIVE_CAP_0 = 0, GPIO_DRIVE_CAP_1, GPIO_DRIVE_CAP_2, GPIO_DRIVE_CAP_3 } gpio_drive_cap_t;

esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t type);
esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t handler, void* arg);
esp_err_t gpio_isr_handler_remove(gpio_num_t pin);
esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t strength);
esp_err_t gpio_sleep_sel_dis(gpio_num_t pin);
