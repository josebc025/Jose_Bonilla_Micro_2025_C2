#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#define EXAMPLE_ESP_WIFI_SSID      "Altice eb28ed"
#define EXAMPLE_ESP_WIFI_PASS      "722-lime-727"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const char *TAG = "wifi_station";
static const char *TAG_MQTT = "mqtt";
static int s_retry_num = 0;

#define ESTADO_INICIAL  0
#define ESTADO_CERRANDO 1
#define ESTADO_ABRIENDO 2
#define ESTADO_CERRADO  3
#define ESTADO_ABIERTO  4
#define ESTADO_ERR      5
#define ESTADO_STOP     6

int EstadoSiguiente = ESTADO_INICIAL;
int EstadoActual = ESTADO_INICIAL;
int EstadoAnterior = ESTADO_INICIAL;
esp_mqtt_client_handle_t global_client;

struct IO {
    unsigned int LSC:1, LSA:1, BA:1, BC:1, BPP:1, BRS:1, SE:1;
    unsigned int MA:1, MC:1, BZZ:1, LAMP:1;
} io;

struct STATUS {
    unsigned int cntTimerCA;
    unsigned int cntRunTimer;
} status = {0, 0};

struct CONFIG {
    unsigned int RunTimer;
    unsigned int TimerCA;
} config = {180, 100};

void ParpadeoRapido() {}
void ParpadeoLamp() {}
void buzzererror() {}

time_t ultimoTiempo = 0;
bool init_estado = true;

int Func_ESTADO_INICIAL(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO INICIAL!\n");
        EstadoAnterior = EstadoActual;
        EstadoActual = ESTADO_INICIAL;
        io.LSC = false;
        io.LSA = false;
        init_estado = false;
    }
    if(io.LSC && io.LSA) return ESTADO_ERR;
    if(io.LSC && !io.LSA) return ESTADO_CERRADO;
    if(!io.LSC && io.LSA) return ESTADO_CERRANDO;
    if(!io.LSC && !io.LSA) return ESTADO_STOP;
    return ESTADO_INICIAL;
}

int Func_ESTADO_CERRANDO(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO CERRANDO!\n");
        EstadoAnterior = EstadoActual;
        EstadoActual = ESTADO_CERRANDO;
        status.cntRunTimer = 0;
        io.MA = false; io.MC = true; io.BA = false; io.BC = false;
        init_estado = false;
    }
    ParpadeoRapido();
    status.cntRunTimer++;
    if(io.LSC) return ESTADO_CERRADO;
    if(io.BA || io.BC) return ESTADO_STOP;
    if(status.cntRunTimer > config.RunTimer) return ESTADO_ERR;
    return ESTADO_CERRANDO;
}

int Func_ESTADO_ABRIENDO(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO ABRIENDO!\n");
        EstadoAnterior = EstadoActual;
        EstadoActual = ESTADO_ABRIENDO;
        status.cntRunTimer = 0;
        io.MA = true; io.MC = false; io.BA = false; io.BC = false;
        init_estado = false;
    }
    ParpadeoLamp();
    status.cntRunTimer++;
    if(io.LSA) return ESTADO_ABIERTO;
    if(io.BA || io.BC || io.BPP) return ESTADO_STOP;
    if(status.cntRunTimer > config.RunTimer) return ESTADO_ERR;
    return ESTADO_ABRIENDO;
}

int Func_ESTADO_CERRADO(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO CERRADO!\n");
        EstadoAnterior = EstadoActual;
        EstadoActual = ESTADO_CERRADO;
        io.MA = false; io.MC = false; io.BA = false;
        init_estado = false;
    }
    if(io.BA || io.BPP) return ESTADO_ABRIENDO;
    return ESTADO_CERRADO;
}

int Func_ESTADO_ABIERTO(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO ABIERTO!\n");
        EstadoAnterior = EstadoActual;
        EstadoActual = ESTADO_ABIERTO;
        io.MA = false; io.MC = false; io.BC = false; io.BPP = false;
        io.LAMP = true;
        ultimoTiempo = time(NULL);
        status.cntTimerCA = 0;
        init_estado = false;
    }
    time_t ahora = time(NULL);
    if (difftime(ahora, ultimoTiempo) >= 1) {
        status.cntTimerCA++;
        printf("LAMPARA ENCENDIDA, PORTON ABIERTO - Tiempo: %u segundos\n", status.cntTimerCA);
        ultimoTiempo = ahora;
    }
    if (io.BC || io.BPP || status.cntTimerCA >= config.RunTimer)
        return ESTADO_CERRANDO;
    return ESTADO_ABIERTO;
}

int Func_ESTADO_ERR(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO ERROR!\n");
        EstadoAnterior = EstadoActual;
        EstadoActual = ESTADO_ERR;
        io.MA = false; io.MC = false;
        init_estado = false;
    }
    buzzererror();
    if(io.BRS) return ESTADO_INICIAL;
    return ESTADO_ERR;
}

int Func_ESTADO_STOP(void) {
    if (init_estado) {
        printf("ESTAMOS EN EL ESTADO STOP!\n");
        EstadoAnterior = EstadoActual;
      
        
       
        init_estado = false;
    }
    if (io.BC && !io.LSC) return ESTADO_CERRANDO;
    if (io.BA && !io.LSA) return ESTADO_ABRIENDO;
    if (io.BA && io.BC) return ESTADO_STOP;
    if (io.BPP && !io.LSA && !io.LSC) return ESTADO_CERRANDO;
    if(io.BRS) return ESTADO_INICIAL;
    return ESTADO_STOP;
}

void ejecutar_maquina_estados(void) {
    int nuevo_estado = EstadoSiguiente;
    switch (EstadoSiguiente) {
        case ESTADO_INICIAL:  nuevo_estado = Func_ESTADO_INICIAL(); break;
        case ESTADO_CERRANDO: nuevo_estado = Func_ESTADO_CERRANDO(); break;
        case ESTADO_ABRIENDO: nuevo_estado = Func_ESTADO_ABRIENDO(); break;
        case ESTADO_CERRADO:  nuevo_estado = Func_ESTADO_CERRADO(); break;
        case ESTADO_ABIERTO:  nuevo_estado = Func_ESTADO_ABIERTO(); break;
        case ESTADO_ERR:      nuevo_estado = Func_ESTADO_ERR(); break;
        case ESTADO_STOP:     nuevo_estado = Func_ESTADO_STOP(); break;
        default:              nuevo_estado = ESTADO_INICIAL; break;
    }
    if (nuevo_estado != EstadoSiguiente) {
        init_estado = true;
        EstadoSiguiente = nuevo_estado;
    }
}

void my_timer_callback(void* arg) {
    ejecutar_maquina_estados();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    global_client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "✅ MQTT conectado");
            esp_mqtt_client_subscribe(global_client, "jose-micro-topic", 0);
            break;

        case MQTT_EVENT_DATA: {
            char msg[50] = {0};
            int len = (event->data_len < sizeof(msg) - 1) ? event->data_len : sizeof(msg) - 1;
            strncpy(msg, event->data, len);
            msg[len] = '\0';

            ESP_LOGI(TAG_MQTT, "Comando recibido: %s", msg);

            if (strcmp(msg, "abrir") == 0) EstadoSiguiente = ESTADO_ABRIENDO;
            else if (strcmp(msg, "cerrar") == 0) EstadoSiguiente = ESTADO_CERRANDO;
            else if (strcmp(msg, "stop") == 0) EstadoSiguiente = ESTADO_STOP;
            else if (strcmp(msg, "reset") == 0) EstadoSiguiente = ESTADO_INICIAL;
            break;
        }

        default:
            break;
    }
}

static void mqtt_app_start(void) {
    ESP_LOGI(TAG_MQTT, "🚀 Iniciando cliente MQTT...");
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.emqx.io:1883/mqtt",
    };
    global_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(global_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(global_client);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "📡 WiFi STA iniciando...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "🔌 Desconectado del WiFi");
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✅ IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    ESP_LOGI(TAG, "⚙️ Inicializando WiFi STA...");

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id, instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    ESP_LOGI(TAG, "🔎 Conectando a SSID: %s", EXAMPLE_ESP_WIFI_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "🚀 WiFi STA iniciado, esperando conexión...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ Conectado correctamente a WiFi");
        mqtt_app_start();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "❌ Falló la conexión a WiFi");
    } else {
        ESP_LOGE(TAG, "⚠️ Evento inesperado en conexión WiFi");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "🚀 Iniciando app_main");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "⚠️ NVS corrupto, borrando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS inicializado");

    wifi_init_sta();

    EstadoSiguiente = ESTADO_STOP;
    init_estado = true;
    ESP_LOGI(TAG, "🟢 Estado inicial cargado");

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &my_timer_callback,
        .name = "my_50ms_timer"
    };

    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_LOGI(TAG, "✅ Timer creado");

    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 50 * 1000));
    ESP_LOGI(TAG, "⏱️ Timer iniciado cada 50 ms");
}
