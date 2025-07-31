#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

// Parámetros para el buzzer
#define PIN_BUZZER       23               // Pin que activa el buzzer o el TIP31
#define FRECUENCIA_TONO  2000             // 2 kHz -> sonido agudo
#define CICLO_TRABAJO    128              // Duty cycle de 50% (8-bit: 128/255)
#define CANAL_BUZZER     LEDC_CHANNEL_0
#define TIMER_BUZZER     LEDC_TIMER_0
#define MODO_BUZZER      LEDC_HIGH_SPEED_MODE

void app_main(void)
{
    // Inicialización del timer para el PWM
    ledc_timer_config_t timer_config = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = FRECUENCIA_TONO,
        .speed_mode = MODO_BUZZER,
        .timer_num = TIMER_BUZZER,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    // Inicialización del canal PWM que controla el buzzer
    ledc_channel_config_t channel_config = {
        .channel = CANAL_BUZZER,
        .gpio_num = PIN_BUZZER,
        .speed_mode = MODO_BUZZER,
        .duty = 0,  // Comienza apagado
        .hpoint = 0,
        .timer_sel = TIMER_BUZZER
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    printf("Generador de tono activo en GPIO %d\n", PIN_BUZZER);

    // Bucle que hace sonar el buzzer por 1 segundo y luego lo apaga por otro segundo
    while (1) {
        // Activar el buzzer
        ledc_set_duty(MODO_BUZZER, CANAL_BUZZER, CICLO_TRABAJO);
        ledc_update_duty(MODO_BUZZER, CANAL_BUZZER);
        printf("Sonando...\n");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Mantener 1 segundo

        // Silenciar el buzzer
        ledc_set_duty(MODO_BUZZER, CANAL_BUZZER, 0);
        ledc_update_duty(MODO_BUZZER, CANAL_BUZZER);
        printf("Silencio.\n");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Mantener 1 segundo
    }
}
