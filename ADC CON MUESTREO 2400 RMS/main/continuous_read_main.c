#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_timer.h"

#define led1 2
#define ledR 33
#define ledG 25
#define ledB 26

#define SAMPLE_PERIOD_US 416
#define SAMPLES_PER_SECOND 2400

static const char *tag = "Main";
uint8_t led_level = 0;
int adc_val = 0;
esp_timer_handle_t adc_timer;

// Arreglo para almacenar muestras
uint32_t suma_cuadrados = 0;
int sample_count = 0;

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

    // Acumulamos el cuadrado
    suma_cuadrados += adc_val * adc_val;
    sample_count++;

    // Calcular RMS cada 2400 muestras (~1 segundo)
    if (sample_count >= SAMPLES_PER_SECOND)
    {
        float rms = sqrt((float)suma_cuadrados / SAMPLES_PER_SECOND);
        ESP_LOGI(tag, "RMS: %.2f", rms);

        // Reset para siguiente segundo
        suma_cuadrados = 0;
        sample_count = 0;
    }

    // (Opcional) LED RGB según adc_val
    int adc_case = adc_val / 1000;
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

void app_main(void)
{
    init_led();
    set_adc();
    set_highres_timer();
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(tag, "Programa iniciado correctamente");
}

// Funciones auxiliares
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

esp_err_t blink_led(void)
{
    led_level = !led_level;
    gpio_set_level(led1, led_level);
    return ESP_OK;
}

esp_err_t set_adc(void)
{
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_width(ADC_WIDTH_BIT_12);
    return ESP_OK;
}

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
