#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/adc.h"

#define led1 2
#define ledR 33
#define ledG 25
#define ledB 26
uint8_t led_level = 0;
static const char *tag = "Main";
TimerHandle_t xTimers;
int interval = 1000;
int timerID = 1;
int adc_val =0;


// Declaraciones
esp_err_t init_led(void);
esp_err_t blink_led(void);
esp_err_t set_timer(void);
esp_err_t set_adc(void);

// Callback del temporizador
void vTimerCallback(TimerHandle_t xTimer)
{
    blink_led();
    adc_val = adc1_get_raw(ADC1_CHANNEL_4);
    int adc_case = adc_val / 1000;
    ESP_LOGI(tag, "ADC VAL: %i", adc_val);
    switch ((adc_case))
    {
    case 0:
        /* code */ 
    gpio_set_level(ledR, 0);
     gpio_set_level(ledG, 0);
      gpio_set_level(ledB, 0);
        break;
         case 1:
        /* code */ 
    gpio_set_level(ledR, 1);
     gpio_set_level(ledG, 0);
      gpio_set_level(ledB, 0);
        break;
         case 2:
        /* code */ 
    gpio_set_level(ledR, 0);
     gpio_set_level(ledG, 1);
      gpio_set_level(ledB, 0);
        break;
    case 3:
    case 4:
        /* code */ 
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
    set_timer();

    // Ya no necesitas blink_led aquí si usas el temporizador
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay largo, el timer ya maneja el parpadeo
    }
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

// Encendido y apagado del LED
esp_err_t blink_led(void)
{
    led_level = !led_level;
    gpio_set_level(led1, led_level);
    return ESP_OK;
}

// Configuración del temporizador
esp_err_t set_timer(void)
{
    ESP_LOGI(tag, "Configuracion inicial del timer");
    xTimers = xTimerCreate("Timer",             // Nombre del temporizador
                           pdMS_TO_TICKS(interval), // Periodo de 1 segundo
                           pdTRUE,              // Auto-reload
                           (void *)timerID,                // ID opcional
                           vTimerCallback);     // Callback

    if (xTimers == NULL)
    {
        ESP_LOGE(tag, "Fallo al crear el temporizador");
        return ESP_FAIL;
    }
    else
    {
        if (xTimerStart(xTimers, 0) != pdPASS)
        {
            ESP_LOGE(tag, "Fallo al iniciar el temporizador");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t set_adc(void)
{
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_width(ADC_WIDTH_BIT_12);
    return ESP_OK;
}
