// Stand-in for esp_sleep.h (PC simulation only): light sleep simply moves
// the simulated clock on, see sim_board.cpp.
#pragma once
#include <cstdint>
#include "esp_err.h"
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_light_sleep_start();
typedef enum { ESP_PD_DOMAIN_RTC_PERIPH = 0 } esp_sleep_pd_domain_t;
typedef enum { ESP_PD_OPTION_OFF = 0, ESP_PD_OPTION_ON, ESP_PD_OPTION_AUTO } esp_sleep_pd_option_t;
esp_err_t esp_sleep_pd_config(esp_sleep_pd_domain_t domain, esp_sleep_pd_option_t option);
