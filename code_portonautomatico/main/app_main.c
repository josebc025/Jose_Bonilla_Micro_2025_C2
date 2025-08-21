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
#include "driver/gpio.h"

/* ========================= Pines =========================
   Entradas LSC/LSA: contactos a GND, activo en 0 (usar pull-up externo).
   GPIO34..39 son solo entrada y no tienen pull interno.
*/
#define PIN_LSC      GPIO_NUM_34   // fin de carrera CERRADO
#define PIN_LSA      GPIO_NUM_35   // fin de carrera ABIERTO
#define PIN_RST      GPIO_NUM_32   // botón RESET (pull-down interno; activo 1)
#define PIN_BTN_ABR  GPIO_NUM_25   // botón ABRIR  (pull-down interno; activo 1)
#define PIN_BTN_CER  GPIO_NUM_26   // botón CERRAR (pull-down interno; activo 1)
#define PIN_BTN_PP   GPIO_NUM_27   // botón PASO-A-PASO (pull-down interno; activo 1)

/* Salidas (relés/driver) */
#define PIN_MA    GPIO_NUM_18   // motor ABRIR
#define PIN_MC    GPIO_NUM_19   // motor CERRAR
#define PIN_BZZ   GPIO_NUM_23   // buzzer
#define PIN_LAMP  GPIO_NUM_5    // lámpara

/* ========================= WiFi ========================= */
#define EXAMPLE_ESP_WIFI_SSID      "Altice eb28ed"
#define EXAMPLE_ESP_WIFI_PASS      "722-lime-727"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const char *TAG = "wifi_station";

/* ========================= MQTT ========================= */
static const char *TAG_MQTT = "mqtt";
#define CMD_TOPIC    "jose-micro-topic"
#define STATE_TOPIC  "jose-micro-topic/estado"
#define STATUS_TOPIC "jose-micro-topic/status"
esp_mqtt_client_handle_t global_client = NULL;

/* ========================= FSM ========================= */
#define ESTADO_INICIAL   0
#define ESTADO_CERRANDO  1
#define ESTADO_ABRIENDO  2
#define ESTADO_CERRADO   3
#define ESTADO_ABIERTO   4
#define ESTADO_ERR       5
#define ESTADO_STOP      6

volatile int  EstadoSiguiente = ESTADO_INICIAL;
volatile int  EstadoActual    = ESTADO_INICIAL;
volatile int  EstadoAnterior  = ESTADO_INICIAL;
volatile bool init_estado     = true;

/* ========================= Helpers ========================= */
static const char* fsm_state_name(int s) {
    switch (s) {
        case ESTADO_INICIAL:  return "INICIAL";
        case ESTADO_CERRANDO: return "CERRANDO";
        case ESTADO_ABRIENDO: return "ABRIENDO";
        case ESTADO_CERRADO:  return "CERRADO";
        case ESTADO_ABIERTO:  return "ABIERTO";
        case ESTADO_ERR:      return "ERROR";
        case ESTADO_STOP:     return "STOP";
        default:              return "DESCONOCIDO";
    }
}

static inline void mqtt_publish_state_transition(int from_s, int to_s) {
    if (!global_client) return;
    char payload[64];
    int n = snprintf(payload, sizeof(payload), "from:%s to:%s",
                     fsm_state_name(from_s), fsm_state_name(to_s));
    esp_mqtt_client_publish(global_client, STATE_TOPIC, payload, n, 0, 0);
}

static inline void mqtt_publish_status_online(void) {
    if (!global_client) return;
    esp_mqtt_client_publish(global_client, STATUS_TOPIC, "online", 0, 0, 1);
    char now_state[32];
    int n = snprintf(now_state, sizeof(now_state), "from:%s to:%s",
                     fsm_state_name(EstadoActual), fsm_state_name(EstadoSiguiente));
    esp_mqtt_client_publish(global_client, STATE_TOPIC, now_state, n, 0, 0);
}

static inline void fsm_request_state(int s) {
    EstadoSiguiente = s;
    init_estado = true;   // habilito acciones de entrada
}

/* Forzar transición inmediata (por límites) */
static inline void fsm_force_state(int s) {
    int from = EstadoSiguiente;
    if (from == s) return;
    mqtt_publish_state_transition(from, s);
    EstadoSiguiente = s;
    init_estado = true;
}

/* ========================= IO lógicos ========================= */
struct IO {
    /* Entradas */
    unsigned int LSC:1, LSA:1, RST:1, BTN_ABR:1, BTN_CER:1, BTN_PP:1;
    /* Salidas */
    unsigned int MA:1, MC:1, BZZ:1, LAMP:1;
} io;

/* ========================= Debounce ========================= */
#define DEBOUNCE_N 3  // 3 muestras * 50ms = 150ms
typedef struct {
    uint8_t cnt_LSC, cnt_LSA, cnt_RST, cnt_BTN_ABR, cnt_BTN_CER, cnt_BTN_PP;
    uint8_t st_LSC,  st_LSA,  st_RST,  st_BTN_ABR,  st_BTN_CER,  st_BTN_PP;
} debounce_t;

static debounce_t db;

/* ========================= Estado/Tiempos ========================= */
struct STATUS {
    unsigned int cntTimerCA;   // segundos en ABIERTO (autocierre)
    unsigned int cntRunTimer;  // ticks de 50 ms en movimiento
} status = {0, 0};

/* ========================= Config ========================= */
struct CONFIG {
    unsigned int OpenTimeoutTicks;   // máx. abriendo
    unsigned int CloseTimeoutTicks;  // máx. cerrando
    unsigned int AutoCloseSeconds;   // autocierre
} config = {
    .OpenTimeoutTicks  = 260,  // 13.0 s
    .CloseTimeoutTicks = 180,  // 9.0 s
    .AutoCloseSeconds  = 20
};

/* ========================= Lámpara/Buzzer ========================= */
static uint8_t blink_slow = 0;   // para ABRIENDO
static uint8_t blink_fast = 0;   // para CERRANDO
static inline void reset_blinks(void) { blink_slow = 0; blink_fast = 0; }

void ParpadeoRapido() {          // 0.25 s ON / 0.25 s OFF
    if (++blink_fast >= 5) { blink_fast = 0; io.LAMP = !io.LAMP; }
}

void ParpadeoLamp() {            // 0.5 s ON / 0.5 s OFF
    if (++blink_slow >= 10) { blink_slow = 0; io.LAMP = !io.LAMP; }
}

void buzzererror() { io.BZZ = 1; }

#define BEEP_ON_TICKS   3
#define BEEP_OFF_TICKS  3
typedef struct { uint8_t active, stage; uint16_t ticks; } beep_t;
static beep_t beep = {0,0,0};

static inline void beep_start_double(void) { beep.active=1; beep.stage=1; beep.ticks=BEEP_ON_TICKS; }
static inline void beep_update(void) {
    if (!beep.active) return;
    switch (beep.stage) { case 1: io.BZZ=1; break; case 2: io.BZZ=0; break; case 3: io.BZZ=1; break; case 4: io.BZZ=0; break; }
    if (beep.ticks>0) { beep.ticks--; return; }
    switch (beep.stage) { case 1: beep.stage=2; beep.ticks=BEEP_OFF_TICKS; break;
                          case 2: beep.stage=3; beep.ticks=BEEP_ON_TICKS;  break;
                          case 3: beep.stage=4; beep.ticks=BEEP_OFF_TICKS; break;
                          default: beep.active=0; beep.stage=0; break; }
}
static inline void beep_cancel(void) { beep.active=0; beep.stage=0; }

/* ========================= GPIO ========================= */
static void gpio_init_app(void) {
    // LSC/LSA sin pull interno
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL<<PIN_LSC) | (1ULL<<PIN_LSA),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    // RST con pull-down interno
    gpio_config_t in_cfg2 = {
        .pin_bit_mask = (1ULL<<PIN_RST),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg2));

    // Botones ABR/CER/PP con pull-down interno
    gpio_config_t in_cfg3 = {
        .pin_bit_mask = (1ULL<<PIN_BTN_ABR) | (1ULL<<PIN_BTN_CER) | (1ULL<<PIN_BTN_PP),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg3));

    // Salidas
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL<<PIN_MA) | (1ULL<<PIN_MC) | (1ULL<<PIN_BZZ) | (1ULL<<PIN_LAMP),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    // Apago todo
    gpio_set_level(PIN_MA, 0);
    gpio_set_level(PIN_MC, 0);
    gpio_set_level(PIN_BZZ, 0);
    gpio_set_level(PIN_LAMP, 0);

    memset(&db, 0, sizeof(db));
}

static inline void debounce_input(uint8_t raw_level, uint8_t *cnt, uint8_t *stable, bool active_low) {
    uint8_t logical = active_low ? (raw_level == 0) : (raw_level == 1);
    if (logical == *stable) { *cnt = 0; }
    else if (++(*cnt) >= DEBOUNCE_N) { *cnt = 0; *stable = logical; }
}

static inline int read_raw_level(gpio_num_t pin) { return gpio_get_level(pin); }

static void read_inputs(void) {
    debounce_input(gpio_get_level(PIN_LSC), &db.cnt_LSC, &db.st_LSC, true);
    debounce_input(gpio_get_level(PIN_LSA), &db.cnt_LSA, &db.st_LSA, true);
    debounce_input(gpio_get_level(PIN_RST), &db.cnt_RST, &db.st_RST, false);
    debounce_input(gpio_get_level(PIN_BTN_ABR), &db.cnt_BTN_ABR, &db.st_BTN_ABR, false);
    debounce_input(gpio_get_level(PIN_BTN_CER), &db.cnt_BTN_CER, &db.st_BTN_CER, false);
    debounce_input(gpio_get_level(PIN_BTN_PP),  &db.cnt_BTN_PP,  &db.st_BTN_PP,  false);

    static int last_LSC=-1,last_LSA=-1,last_RST=-1,last_ABR=-1,last_CER=-1,last_PP=-1;

    io.LSC     = db.st_LSC;
    io.LSA     = db.st_LSA;
    io.RST     = db.st_RST;
    io.BTN_ABR = db.st_BTN_ABR;
    io.BTN_CER = db.st_BTN_CER;
    io.BTN_PP  = db.st_BTN_PP;

    if (io.LSC != last_LSC) { ESP_LOGI("IO", io.LSC ? "LSC ACTIVADO" : "LSC LIBERADO"); last_LSC = io.LSC; }
    if (io.LSA != last_LSA) { ESP_LOGI("IO", io.LSA ? "LSA ACTIVADO" : "LSA LIBERADO"); last_LSA = io.LSA; }
    if (io.RST != last_RST) { ESP_LOGI("IO", io.RST ? "RST PULSADO"  : "RST LIBERADO"); last_RST = io.RST; }
    if (io.BTN_ABR != last_ABR) { ESP_LOGI("IO", io.BTN_ABR ? "ABRIR PULSADO" : "ABRIR LIBERADO"); last_ABR = io.BTN_ABR; }
    if (io.BTN_CER != last_CER) { ESP_LOGI("IO", io.BTN_CER ? "CERRAR PULSADO": "CERRAR LIBERADO"); last_CER = io.BTN_CER; }
    if (io.BTN_PP  != last_PP ) { ESP_LOGI("IO", io.BTN_PP  ? "PP PULSADO"    : "PP LIBERADO");     last_PP  = io.BTN_PP; }
}

static void apply_outputs(void) {
    if (io.MA && io.MC) { ESP_LOGE("IO", "MA y MC activos a la vez, corto ambos."); io.MA = io.MC = 0; }
    gpio_set_level(PIN_MA,   io.MA);
    gpio_set_level(PIN_MC,   io.MC);
    gpio_set_level(PIN_BZZ,  io.BZZ);
    gpio_set_level(PIN_LAMP, io.LAMP);
}

/* ========================= FSM ========================= */
time_t ultimoTiempo = 0;

static inline int enforce_limits_on_entry(int target_state) {
    if (io.LSC && io.LSA) return ESTADO_ERR;
    if (target_state == ESTADO_ABRIENDO && io.LSA) return ESTADO_ABIERTO;
    if (target_state == ESTADO_CERRANDO && io.LSC) return ESTADO_CERRADO;
    return target_state;
}

int Func_ESTADO_INICIAL(void) {
    if (init_estado) {
        ESP_LOGI("FSM", "INICIAL");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_INICIAL;
        io.MA = io.MC = io.LAMP = io.BZZ = 0;
        reset_blinks(); init_estado = false;
    }
    if (io.LSC && io.LSA) return ESTADO_ERR;
    if (io.LSC && !io.LSA) return ESTADO_CERRADO;
    if (!io.LSC && io.LSA) return ESTADO_ABIERTO;
    return ESTADO_STOP;
}

int Func_ESTADO_CERRANDO(void) {
    if (init_estado) {
        ESP_LOGI("FSM", "CERRANDO");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_CERRANDO;
        if (io.LSC) return ESTADO_CERRADO;
        status.cntRunTimer = 0; io.MA = 0; io.MC = 1; io.LAMP = 1; io.BZZ = 0;
        blink_fast = 0; beep_start_double(); init_estado = false;
    }
    if (io.LSC && io.LSA) return ESTADO_ERR;
    ParpadeoRapido(); status.cntRunTimer++;
    if (io.LSC) return ESTADO_CERRADO;
    if (status.cntRunTimer > config.CloseTimeoutTicks) return ESTADO_ERR;
    return ESTADO_CERRANDO;
}

int Func_ESTADO_ABRIENDO(void) {
    if (init_estado) {
        ESP_LOGI("FSM", "ABRIENDO");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_ABRIENDO;
        if (io.LSA) return ESTADO_ABIERTO;
        status.cntRunTimer = 0; io.MA = 1; io.MC = 0; io.LAMP = 1; io.BZZ = 0;
        blink_slow = 0; beep_start_double(); init_estado = false;
    }
    if (io.LSC && io.LSA) return ESTADO_ERR;
    ParpadeoLamp(); status.cntRunTimer++;
    if (io.LSA) return ESTADO_ABIERTO;
    if (status.cntRunTimer > config.OpenTimeoutTicks) return ESTADO_ERR;
    return ESTADO_ABRIENDO;
}

int Func_ESTADO_CERRADO(void) {
    if (init_estado) {
        ESP_LOGI("FSM", "CERRADO");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_CERRADO;
        io.MA = io.MC = 0; io.LAMP = 0; io.BZZ = 0; reset_blinks(); init_estado = false;
    }
    if (io.LSC && io.LSA) return ESTADO_ERR;
    if (!io.LSC && !io.LSA) return ESTADO_STOP;
    if (!io.LSC && io.LSA)  return ESTADO_ABIERTO;
    return ESTADO_CERRADO;
}

int Func_ESTADO_ABIERTO(void) {
    if (init_estado) {
        ESP_LOGI("FSM", "ABIERTO");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_ABIERTO;
        io.MA = 0; io.MC = 0; io.LAMP = 1; io.BZZ = 0; reset_blinks();
        ultimoTiempo = time(NULL); status.cntTimerCA = 0; init_estado = false;
    }
    if (io.LSC && io.LSA) return ESTADO_ERR;

    time_t ahora = time(NULL);
    if (difftime(ahora, ultimoTiempo) >= 1) {
        status.cntTimerCA++; ESP_LOGI("FSM", "ABIERTO %u s", status.cntTimerCA); ultimoTiempo = ahora;
    }
    if (status.cntTimerCA >= config.AutoCloseSeconds) return ESTADO_CERRANDO;
    if (!io.LSA && !io.LSC) return ESTADO_STOP;
    return ESTADO_ABIERTO;
}

int Func_ESTADO_ERR(void) {
    if (init_estado) {
        ESP_LOGE("FSM", "ERROR");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_ERR;
        io.MA = 0; io.MC = 0; io.LAMP = 0; io.BZZ = 1; reset_blinks(); init_estado = false;
    }
    buzzererror();
    return ESTADO_ERR;
}

int Func_ESTADO_STOP(void) {
    if (init_estado) {
        ESP_LOGW("FSM", "STOP");
        EstadoAnterior = EstadoActual; EstadoActual = ESTADO_STOP;
        io.MA = 0; io.MC = 0; io.LAMP = 0; io.BZZ = 0; reset_blinks(); init_estado = false;
    }
    if (io.LSC && io.LSA) return ESTADO_ERR;
    return ESTADO_STOP;
}

static void ejecutar_maquina_estados(void) {
    int nuevo_estado = EstadoSiguiente;
    switch (EstadoSiguiente) {
        case ESTADO_INICIAL:  nuevo_estado = Func_ESTADO_INICIAL();  break;
        case ESTADO_CERRANDO: nuevo_estado = Func_ESTADO_CERRANDO(); break;
        case ESTADO_ABRIENDO: nuevo_estado = Func_ESTADO_ABRIENDO(); break;
        case ESTADO_CERRADO:  nuevo_estado = Func_ESTADO_CERRADO();  break;
        case ESTADO_ABIERTO:  nuevo_estado = Func_ESTADO_ABIERTO();  break;
        case ESTADO_ERR:      nuevo_estado = Func_ESTADO_ERR();      break;
        case ESTADO_STOP:     nuevo_estado = Func_ESTADO_STOP();     break;
        default:              nuevo_estado = ESTADO_INICIAL;         break;
    }
    if (nuevo_estado != EstadoSiguiente) {
        mqtt_publish_state_transition(EstadoSiguiente, nuevo_estado);
        init_estado = true; EstadoSiguiente = nuevo_estado;
    }
}

/* ========================= Heartbeat (2 s) ========================= */
static void print_status_heartbeat(void) {
    int raw_LSC = read_raw_level(PIN_LSC);
    int raw_LSA = read_raw_level(PIN_LSA);
    int raw_RST = read_raw_level(PIN_RST);
    int raw_ABR = read_raw_level(PIN_BTN_ABR);
    int raw_CER = read_raw_level(PIN_BTN_CER);
    int raw_PP  = read_raw_level(PIN_BTN_PP);
    ESP_LOGI("HB",
             "[%s] next:%s | LSC(raw:%d,deb:%u) LSA(raw:%d,deb:%u) RST(raw:%d,deb:%u) ABR(raw:%d,deb:%u) CER(raw:%d,deb:%u) PP(raw:%d,deb:%u) | MA:%u MC:%u BZZ:%u LAMP:%u",
             fsm_state_name(EstadoActual), fsm_state_name(EstadoSiguiente),
             raw_LSC, io.LSC, raw_LSA, io.LSA, raw_RST, io.RST,
             raw_ABR, io.BTN_ABR, raw_CER, io.BTN_CER, raw_PP, io.BTN_PP,
             io.MA, io.MC, io.BZZ, io.LAMP);
}

/* ========================= Timer 50 ms =========================
   Overrides de límites:
   - ABRIENDO + LSA=1  -> ABIERTO
   - CERRANDO + LSC=1  -> CERRADO
   - STOP/INICIAL con LSA=1 -> ABIERTO; con LSC=1 -> CERRADO
*/
static void my_timer_callback(void* arg) {
    static uint16_t hb_ticks = 0;     // 40 * 50 ms = 2 s
    static bool rst_armed = true;
    static bool abr_armed = true;
    static bool cer_armed = true;
    static bool pp_armed  = true;

    read_inputs();

    // RESET por flanco
    if (io.RST && rst_armed) { fsm_request_state(ESTADO_INICIAL); rst_armed = false; }
    else if (!io.RST && !rst_armed) rst_armed = true;

    // ABRIR
    if (io.BTN_ABR && abr_armed) {
        int target = enforce_limits_on_entry(ESTADO_ABRIENDO);
        fsm_request_state(target); abr_armed = false;
    } else if (!io.BTN_ABR && !abr_armed) abr_armed = true;

    // CERRAR
    if (io.BTN_CER && cer_armed) {
        int target = enforce_limits_on_entry(ESTADO_CERRANDO);
        fsm_request_state(target); cer_armed = false;
    } else if (!io.BTN_CER && !cer_armed) cer_armed = true;

    // PASO-A-PASO: CERRADO->ABRIR, ABIERTO->CERRAR, DESCONOCIDO->CERRAR
    if (io.BTN_PP && pp_armed) {
        int target;
        if (io.LSC && !io.LSA)      target = enforce_limits_on_entry(ESTADO_ABRIENDO);
        else if (io.LSA && !io.LSC) target = enforce_limits_on_entry(ESTADO_CERRANDO);
        else                         target = enforce_limits_on_entry(ESTADO_CERRANDO);
        fsm_request_state(target); pp_armed = false;
    } else if (!io.BTN_PP && !pp_armed) pp_armed = true;

    // Interlock de seguridad
    if (io.LSC && io.LSA) {
        if (EstadoSiguiente != ESTADO_ERR) {
            mqtt_publish_state_transition(EstadoSiguiente, ESTADO_ERR);
            EstadoSiguiente = ESTADO_ERR; init_estado = true;
        }
    } else {
        switch (EstadoSiguiente) {
            case ESTADO_ABRIENDO: if (io.LSA) fsm_force_state(ESTADO_ABIERTO); break;
            case ESTADO_CERRANDO: if (io.LSC) fsm_force_state(ESTADO_CERRADO); break;
            case ESTADO_STOP:
            case ESTADO_INICIAL:
                if (io.LSA) fsm_force_state(ESTADO_ABIERTO);
                else if (io.LSC) fsm_force_state(ESTADO_CERRADO);
                break;
            default: break;
        }
    }

    ejecutar_maquina_estados();

    if (EstadoSiguiente == ESTADO_ERR) { beep_cancel(); io.BZZ = 1; }
    else { beep_update(); }

    apply_outputs();

    if (++hb_ticks >= 40) { hb_ticks = 0; print_status_heartbeat(); }
}

/* ========================= Utils MQTT ========================= */
static inline void str_trim_lower(char *s) {
    char *p = s; while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if (p!=s) memmove(s,p,strlen(p)+1);
    int L = (int)strlen(s);
    while (L>0 && (s[L-1]==' '||s[L-1]=='\t'||s[L-1]=='\r'||s[L-1]=='\n')) s[--L]='\0';
    for (int i=0;s[i];++i) if (s[i]>='A'&&s[i]<='Z') s[i]=(char)(s[i]-'A'+'a');
}

/* ========================= MQTT ========================= */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    global_client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT conectado");
            esp_mqtt_client_subscribe(global_client, CMD_TOPIC, 0);
            ESP_LOGI(TAG_MQTT, "Suscrito a %s (QoS0)", CMD_TOPIC);
            mqtt_publish_status_online();
            break;

        case MQTT_EVENT_DATA: {
            if (!(event->topic && event->topic_len == strlen(CMD_TOPIC) &&
                  strncmp(event->topic, CMD_TOPIC, event->topic_len) == 0)) break;
            if (event->total_data_len != event->data_len || event->current_data_offset != 0) break;
            if (event->data_len <= 0) break;

            char msg[64]={0};
            int len = (event->data_len < (int)sizeof(msg)-1)? event->data_len : (int)sizeof(msg)-1;
            memcpy(msg, event->data, len); msg[len]='\0'; str_trim_lower(msg);
            if (msg[0]=='\0') break;

            ESP_LOGI(TAG_MQTT, "Comando: %s", msg);

            if (!strcmp(msg,"abrir")) {
                int target = enforce_limits_on_entry(ESTADO_ABRIENDO);
                fsm_request_state(target);
            } else if (!strcmp(msg,"cerrar")) {
                int target = enforce_limits_on_entry(ESTADO_CERRANDO);
                fsm_request_state(target);
            } else if (!strcmp(msg,"pp")) {
                int target;
                if (io.LSC && !io.LSA)      target = enforce_limits_on_entry(ESTADO_ABRIENDO);
                else if (io.LSA && !io.LSC) target = enforce_limits_on_entry(ESTADO_CERRANDO);
                else                         target = enforce_limits_on_entry(ESTADO_CERRANDO);
                fsm_request_state(target);
            } else if (!strcmp(msg,"stop")) {
                fsm_request_state(ESTADO_STOP);
            } else if (!strcmp(msg,"reset")) {
                fsm_request_state(ESTADO_INICIAL);
            } else {
                ESP_LOGW(TAG_MQTT, "Comando desconocido: '%s'", msg);
            }
            break;
        }

        case MQTT_EVENT_DISCONNECTED: ESP_LOGW(TAG_MQTT, "MQTT desconectado"); break;
        case MQTT_EVENT_ERROR:        ESP_LOGE(TAG_MQTT, "MQTT EVENT ERROR");  break;
        default: break;
    }
}

static void mqtt_app_start(void) {
    ESP_LOGI(TAG_MQTT, "Iniciando cliente MQTT...");
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.emqx.io:1883",
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false,
        .task.priority = 5
    };
    global_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(global_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(global_client);
}

/* ========================= Wi-Fi ========================= */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA iniciando..."); esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Desconectado del WiFi");
        static int s_retry_num = 0;
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) { esp_wifi_connect(); s_retry_num++; }
        else { xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
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
        .sta = { .ssid = EXAMPLE_ESP_WIFI_SSID, .password = EXAMPLE_ESP_WIFI_PASS, .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) { mqtt_app_start(); }
    else if (bits & WIFI_FAIL_BIT) { ESP_LOGE(TAG, "Fallo la conexion a WiFi"); }
    else { ESP_LOGE(TAG, "Evento inesperado en WiFi"); }
}

/* ========================= app_main ========================= */
void app_main(void) {
    ESP_LOGI(TAG, "Inicio");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_init_app();
    wifi_init_sta();

    fsm_request_state(ESTADO_STOP);

    const esp_timer_create_args_t periodic_timer_args = { .callback = &my_timer_callback, .name = "my_50ms_timer" };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 50 * 1000)); // 50 ms
    ESP_LOGI(TAG, "Timer 50 ms iniciado");
}
