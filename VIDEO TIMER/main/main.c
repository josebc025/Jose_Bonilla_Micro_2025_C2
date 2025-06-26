#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define led1 2

uint8_t led_level = 0;
static const char *tag = "Main";
TimerHandle_t xTimers;
int interval = 1000;
int timerID = 1;

// Declaraciones
esp_err_t init_led(void);
esp_err_t blink_led(void);
esp_err_t set_timer(void);

// Callback del temporizador
void vTimerCallback(TimerHandle_t xTimer)
{
    blink_led();
    ESP_LOGI(tag, "Temporizador activado, LED toggled");
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
