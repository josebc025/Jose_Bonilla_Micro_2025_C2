#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_timer.h"  // <- MUY IMPORTANTE

#define led1 2
#define ledR 33
#define ledG 25
#define ledB 26

#define SAMPLE_PERIOD_US 416  // ~2400 muestras por segundo

static const char *tag = "Main";
uint8_t led_level = 0;
int adc_val = 0;
esp_timer_handle_t adc_timer;  // Timer de alta resolución

// Declaraciones
esp_err_t init_led(void);
esp_err_t blink_led(void);
esp_err_t set_adc(void);
esp_err_t set_highres_timer(void);

// Callback del muestreo ADC
void vTimerCallback(void *arg)
{
    blink_led();
    adc_val = adc1_get_raw(ADC1_CHANNEL_4);
    int adc_case = adc_val / 1000;
    ESP_LOGI(tag, "ADC VAL: %i", adc_val);

    switch (adc_case)
    {
    case 0:
        gpio_set_level(ledR, 0);
        gpio_set_level(ledG, 0);
        gpio_set_level(ledB, 0);
        break;

    case 1:
        gpio_set_level(ledR, 1);
        gpio_set_level(ledG, 0);
        gpio_set_level(ledB, 0);
        break;

    case 2:
        gpio_set_level(ledR, 0);
        gpio_set_level(ledG, 1);
        gpio_set_level(ledB, 0);
        break;

    case 3:
    case 4:
        gpio_set_level(ledR, 1);
        gpio_set_level(ledG, 1);
        gpio_set_level(ledB, 1);
        break;

    default:
        break;
    }
}

// Función principal
void app_main(void)
{
    init_led();
    set_adc();              // Inicializa el ADC
    set_highres_timer();    // Inicia el muestreo periódico
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(tag, "Programa iniciado correctamente");

    // No necesitas while(1), el muestreo se hace por interrupción
}

// Inicialización del LED
esp_err_t init_led(void)
{
    gpio_reset_pin(led1);
    gpio_set_direction(led1, GPIO_MODE_OUTPUT);

    gpio_reset_pin(ledR);
    gpio_set_direction(ledR, GPIO_MODE_OUTPUT);

    gpio_reset_pin(ledG);
    gpio_set_direction(ledG, GPIO_MODE_OUTPUT);

    gpio_reset_pin(ledB);
    gpio_set_direction(ledB, GPIO_MODE_OUTPUT);

    return ESP_OK;
}

// Encendido y apagado del LED1 (parpadeo)
esp_err_t blink_led(void)
{
    led_level = !led_level;
    gpio_set_level(led1, led_level);
    return ESP_OK;
}

// Configuración del ADC
esp_err_t set_adc(void)
{
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_width(ADC_WIDTH_BIT_12);
    return ESP_OK;
}

// Configuración del muestreo con esp_timer
esp_err_t set_highres_timer(void)
{
    const esp_timer_create_args_t adc_timer_args = {
        .callback = &vTimerCallback,
        .name = "adc_sample_timer"};

    ESP_ERROR_CHECK(esp_timer_create(&adc_timer_args, &adc_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(adc_timer, SAMPLE_PERIOD_US));

    ESP_LOGI(tag, "High-res ADC timer started (416 us interval)");
    return ESP_OK;
}
