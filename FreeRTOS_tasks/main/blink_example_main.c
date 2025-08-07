#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"


#define ledR 33
#define ledG 25
#define ledB 26
#define  STACK_SIZE 1024*2
#define R_delay 1000
#define G_delay 2000
#define B_delay 4000

const char *tag = "Main"; 
esp_err_t init_led (void);
esp_err_t create_tasks (void);
void vTaskR( void * pvParameters );
void vTaskG( void * pvParameters );
void vTaskB( void * pvParameters );

void app_main(void)
{
  init_led();
  create_tasks ();
  while (1)
  {

    vTaskDelay (pdMS_TO_TICKS (500));
    printf ("hello from main \n"); 
  }
  
  
} 

esp_err_t init_led ()
{
   gpio_reset_pin (ledR);
  gpio_set_direction (ledR, GPIO_MODE_OUTPUT);
   gpio_reset_pin (ledG);
  gpio_set_direction (ledG, GPIO_MODE_OUTPUT);
   gpio_reset_pin (ledB);
  gpio_set_direction (ledB, GPIO_MODE_OUTPUT);
  return ESP_OK; 
} 

esp_err_t create_tasks (void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    xTaskCreate( vTaskR, "vTaskR", STACK_SIZE, &ucParameterToPass, 1, &xHandle );
   configASSERT( xHandle );

   xTaskCreate( vTaskG, "vTaskG", STACK_SIZE, &ucParameterToPass, 1, &xHandle );
   configASSERT( xHandle );

   xTaskCreate( vTaskB, "vTaskB", STACK_SIZE, &ucParameterToPass, 1, &xHandle );
   configASSERT( xHandle );
 
    return ESP_OK;

}
void vTaskR( void * pvParameters )
  {
    while ( 1 )
    {
        ESP_LOGI(tag, "Led R"); 
        gpio_set_level(ledR, 1);
    vTaskDelay(pdMS_TO_TICKS(R_delay));

     gpio_set_level(ledR, 0);
    vTaskDelay(pdMS_TO_TICKS(R_delay));
    }
  }

  void vTaskG( void * pvParameters )
  {
    while ( 1 )
    {
        ESP_LOGW(tag, "Led G"); 
        gpio_set_level(ledG, 1);
    vTaskDelay(pdMS_TO_TICKS(G_delay));

     gpio_set_level(ledG, 0);
    vTaskDelay(pdMS_TO_TICKS(G_delay));
    }
  }

  void vTaskB( void * pvParameters )
  {
    while ( 1 )
    {
        ESP_LOGE(tag, "Led B"); 
        gpio_set_level(ledB, 1);
    vTaskDelay(pdMS_TO_TICKS(B_delay));

     gpio_set_level(ledB, 0);
    vTaskDelay(pdMS_TO_TICKS(B_delay));
    }
  }
