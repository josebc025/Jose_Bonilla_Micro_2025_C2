#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

// Definiciones para el control por PWM
#define PIN_PWM         18              // Pin conectado al transistor TIP31
#define FRECUENCIA_PWM  25000           // 25 kHz para evitar ruidos molestos
#define RESOLUCION_PWM  LEDC_TIMER_8_BIT
#define CANAL_PWM       LEDC_CHANNEL_0
#define MODO_VELOCIDAD  LEDC_HIGH_SPEED_MODE

void app_main(void)
{
    // Inicialización del timer para generar la señal PWM
    ledc_timer_config_t config_timer = {
        .duty_resolution = RESOLUCION_PWM,
        .freq_hz = FRECUENCIA_PWM,
        .speed_mode = MODO_VELOCIDAD,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&config_timer));

    // Parámetros para el canal PWM
    ledc_channel_config_t config_channel = {
        .gpio_num = PIN_PWM,
        .speed_mode = MODO_VELOCIDAD,
        .channel = CANAL_PWM,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,      // Comenzar con duty cycle en 0
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&config_channel));

    // Bucle infinito que incrementa y luego reduce el valor del duty cycle
    while (1) {
        // Incremento gradual del duty
        for (int i = 0; i <= 255; i += 5) {
            ledc_set_duty(MODO_VELOCIDAD, CANAL_PWM, i);
            ledc_update_duty(MODO_VELOCIDAD, CANAL_PWM);
            printf("Subiendo PWM: duty = %d\n", i);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        // Decremento gradual del duty
        for (int i = 255; i >= 0; i -= 5) {
            ledc_set_duty(MODO_VELOCIDAD, CANAL_PWM, i);
            ledc_update_duty(MODO_VELOCIDAD, CANAL_PWM);
            printf("Bajando PWM: duty = %d\n", i);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
