#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h> // libreria booleanos.
#include <time.h> // libreria para usar comandos de tiempo.

//DEFINICION de estados.
#define ESTADO_INICIAL 0
#define ESTADO_CERRANDO 1
#define ESTADO_ABRIENDO 2
#define ESTADO_CERRADO 3
#define ESTADO_ABIERTO 4
#define ESTADO_ERR 5
#define ESTADO_STOP 6

//definicion de funciones para los estados.
int Func_ESTADO_INICIAL(void);
int Func_ESTADO_CERRANDO(void);
int Func_ESTADO_ABRIENDO(void);
int Func_ESTADO_CERRADO(void);
int Func_ESTADO_ABIERTO(void);
int Func_ESTADO_ERR(void);
int Func_ESTADO_STOP(void);
void ParpadeoRapido(void);
void ParpadeoLamp(void);
void buzzererror(void);
// variables para funciones lampara y buzzer.
bool estadoLamp = false;
time_t tiempoUltimoCambio = 0;
bool estadobuzzer = false;


//variables globales de estado
int EstadoSiguiente = ESTADO_INICIAL;
int EstadoActual = ESTADO_INICIAL;
int EstadoAnterior = ESTADO_INICIAL;

//estructura para todas las entradas y salidas.
struct IO
{  //DEFINICION DE ENTRADAS Y SALIDAS.
    unsigned int LSC:1;//entrada limitswitch de puerta cerrada
    unsigned int LSA:1;//entrada limitswitch de puerta abierta
    unsigned int BA:1;//Boton abrir
    unsigned int BC:1;//Boton cerrar
    unsigned int BPP:1;//Boton PP varias funciones
    unsigned int BRS:1;//Boton RESET SISTEMA
    unsigned int SE:1;//Entrada de Stop Emergency
    unsigned int MA:1;//Salida motor direccion abrir
    unsigned int MC:1;//Salida motor direccion cerrar
    unsigned int BZZ:1;//Salida BUZZER
    unsigned int LAMP:1;//Salida LAMPARA
}io;

//estructura para variables de timers.
struct STATUS {
    unsigned int cntTimerCA;    // Tiempo de cierre automático en segundos
    unsigned int cntRunTimer;   // Tiempo de rodamiento en segundos
};
struct STATUS status = {0, 0};
//estructura para variables usadas para comparar las variables de timers.
struct CONFIG {
    unsigned int RunTimer;
    unsigned int TimerCA;
};
struct CONFIG config = {180, 100};

//maquina de estados. en el main proceso principal de codigo en C.
int main()
{
    for(;;)
    {
        if(EstadoSiguiente == ESTADO_INICIAL)
        {
            EstadoSiguiente = Func_ESTADO_INICIAL();
        }
        if(EstadoSiguiente == ESTADO_ABIERTO)
        {
            EstadoSiguiente = Func_ESTADO_ABIERTO();
        }
        if(EstadoSiguiente == ESTADO_ABRIENDO)
        {
            EstadoSiguiente = Func_ESTADO_ABRIENDO();
        }
        if(EstadoSiguiente == ESTADO_CERRADO)
        {
            EstadoSiguiente = Func_ESTADO_CERRADO();
        }
        if(EstadoSiguiente == ESTADO_CERRANDO)
        {
            EstadoSiguiente = Func_ESTADO_CERRANDO();
        }
        if(EstadoSiguiente == ESTADO_ERR)
        {
            EstadoSiguiente = Func_ESTADO_ERR();
        }
        if(EstadoSiguiente == ESTADO_STOP)
        {
            EstadoSiguiente = Func_ESTADO_STOP();
        }
    }
    return 0;
}
//funcion estado inicial.
int Func_ESTADO_INICIAL(void)
{
    printf("ESTAMOS EN EL ESTADO INICIAL!\n");
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_INICIAL;
    io.LSC = false;
    io.LSA = false;
    //verifica si existe un error
    if(io.LSC == true && io.LSA == true)
    {
        return ESTADO_ERR;
    }
    //puerta cerrada
    if(io.LSC == true && io.LSA == false)
    {
        return ESTADO_CERRADO;
    }
    //puerta abierta
    if(io.LSC == false && io.LSA == true)
    {
        return ESTADO_CERRANDO;
    }
    //puerta en estado desconocido
    if(io.LSC == false && io.LSA == false)
    {
        return ESTADO_STOP;
    }
}

//funcion estado CERRANDO
int Func_ESTADO_CERRANDO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRANDO;
    //funciones de estado estaticas (una sola vez)
     status.cntRunTimer = 0;//reinicio del timer
    io.MA = false;
    io.MC = true;
    io.BA = false;
    io.BC = false;
    //io.LSC = true;
    // CONTROLAR LAMPARA
    printf("ESTAMOS EN EL ESTADO CERRANDO!\n");

    //ciclo de estado
    for(;;)
    {
        //funcion para encender luz intermitente
        ParpadeoRapido();
        if(io.LSC == true)
        {
           return ESTADO_CERRADO;
        }
        if(io.BA == true || io.BC == true)
        {
          return ESTADO_STOP;
        }
        //verifica error de run timer
        if(status.cntRunTimer > config.RunTimer)
        {
            return ESTADO_ERR;
        }
    }
}

//funcion estado ABRIENDO.
int Func_ESTADO_ABRIENDO(void)
{
    printf("ESTAMOS EN EL ESTADO ABRIENDO!\n");
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABRIENDO;
    //funciones de estado estaticas (una sola vez)
    status.cntRunTimer = 0;//reinicio del timer
    io.MA = true; // como estoy abriendo prende el motor MA
    io.MC = false; //  APAGA MC
    io.BA = false; // SI ESTE BOTON ESTABA ACTIVO APAGALO
    io.BC = false; // SI ESTE BOTON ESTABA ACTIVO APAGALO
    //ciclo de estado
    for(;;)
    {
        //funcion para encender luz intermitente
        ParpadeoLamp();
        // Si llegamos al limite pasamos a abierto.
        if(io.LSA == true)
        {
           return ESTADO_ABIERTO;
        }
        // si se presiona algun boton durante el proceso nos detenemos.
        if(io.BA == true || io.BC == true || io.BPP == true )
        {
          return ESTADO_STOP;
        }
        //verifica error de run timer
        if(status.cntRunTimer > config.RunTimer)
        {
            return ESTADO_ERR;
        }
    }
}

//funcion estado CERRADO.
int Func_ESTADO_CERRADO(void)
{
    printf("ESTAMOS EN EL ESTADO CERRADO!\n");
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRADO;
    //funciones de estado estaticas (una sola vez)
    io.MA = false;
    io.MC = false;
    io.BA = false;
    //ciclo de estado
    for(;;)
    {
        if(io.BA == true)
        {
            return ESTADO_ABRIENDO;
        }
        // SI SE PRESIONA EL BOTON BPP EN  ESTADO CERRADO, ABRIMOS.
        if (io.BPP == true){
            return ESTADO_ABRIENDO;
        }
    }
}

//funcion estado ABIERTO.
int Func_ESTADO_ABIERTO(void)
{
    printf("ESTAMOS EN EL ESTADO ABIERTO!\n");
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABIERTO;

    io.MA = false;
    io.MC = false;
    io.BC = false;
    io.BPP = false;
    io.LAMP = true; // Enciende la lámpara una vez al entrar

    time_t ultimoTiempo = time(NULL);
    status.cntTimerCA = 0;

    for (;;)
    {
        // Mostrar mensaje solo una vez cada segundo
        time_t ahora = time(NULL);
        if (difftime(ahora, ultimoTiempo) >= 1)
        {
            status.cntTimerCA++;
            printf("LAMPARA ENCENDIDA, PORTON ABIERTO - Tiempo: %u segundos\n", status.cntTimerCA);
            fflush(stdout);
            ultimoTiempo = ahora;
        }

        if (io.BC == true || io.BPP == true)
        {
            return ESTADO_CERRANDO;
        }

        if (status.cntTimerCA >= config.RunTimer)
        {
            return ESTADO_CERRANDO;
        }
    }
}

//funcion estado ERROR.
int Func_ESTADO_ERR(void)
{
    printf("ESTAMOS EN EL ESTADO ERROR!\n");
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ERR;
    //apagamos los motores
    io.MA = false;
    io.MC = false;
    for(;;)
    {   //nos mantenemos
        buzzererror();

        if(io.BRS == true){

        return ESTADO_INICIAL;
        }
    }
}

//funcion estado stop.
int Func_ESTADO_STOP(void)
{printf("ESTAMOS EN EL ESTADO STOP!\n");
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_STOP;
    //apagamos los motores
    io.MA = false; // apagame el motor MA
    io.MC = false;// APAGAME EL MOTOR MC

     for(;;)
    {   //PARA CERRAR DESDE STOP, Presionamos boton cerrar y Verificamos si el LSC esta desactivado.
        if (io.BC == true && io.LSC == false)
        {
            return ESTADO_CERRANDO;
        }
         //PARA ABRIR DESDE STOP. Presionamos boton abrir y Verificamos si el LSA esta desactivado.
        if (io.BA == true && io.LSA == false)
        {
            return ESTADO_ABRIENDO;
        }
        // Si presionamos ambos botones mantenernos en estado stop.
        if (io.BA == true && io.BC == true)
        {
            return ESTADO_STOP;
        }
        //SI LA PUERTA ESTA PARADA EN MEDIO SE CIERRA. con el boton BPP.
        if (io.BPP == true && io.LSA == false && io.LSC == false){
            return ESTADO_CERRANDO;
        }
        if(io.BRS == true){

        return ESTADO_INICIAL;
        }
    }
}

// funcion para que parpaedee la bombilla A 0.5S
void ParpadeoLamp()
{
    time_t tiempoActual = time(NULL); // Tiempo en segundos

    // ¿Han pasado 5 segundos desde el último cambio?
    if (difftime(tiempoActual, tiempoUltimoCambio) >= 0.5)
    {
        estadoLamp = !estadoLamp; // Alterna estado
        io.LAMP = estadoLamp;     // Aplica al sistema
        io.BZZ = estadoLamp; // BUZZER a la par con la lampara.

        if (estadoLamp)
            printf("Lámpara ENCENDIDA\n");
        else
            printf("Lámpara APAGADA\n");

        fflush(stdout); // Asegura impresión inmediata

        tiempoUltimoCambio = tiempoActual; // Actualiza el tiempo
    }
}


// funcion para que parpaedee la bombilla A 0.25S
void ParpadeoRapido()
{
    time_t tiempoActual = time(NULL); // Tiempo en segundos

    // ¿Han pasado 5 segundos desde el último cambio?
    if (difftime(tiempoActual, tiempoUltimoCambio) >= 0.25)
    {
        estadoLamp = !estadoLamp; // Alterna estado
        io.LAMP = estadoLamp;     // Aplica al sistema
        io.BZZ = estadoLamp; // BUZZER a la par con la lampara.
        if (estadoLamp)
            printf("Lámpara ENCENDIDA\n");
        else
            printf("Lámpara APAGADA\n");

        fflush(stdout); // Asegura impresión inmediata

        tiempoUltimoCambio = tiempoActual; // Actualiza el tiempo
    }
}


// funcion para buzzer error.
void buzzererror()
{
    time_t tiempoActual = time(NULL); // Tiempo en segundos

    // ¿Han pasado 5 segundos desde el último cambio?
    if (difftime(tiempoActual, tiempoUltimoCambio) >= 10)
    {
        estadobuzzer = !estadobuzzer; // Alterna estado
        io.BZZ = estadobuzzer; // BUZZER a la par con la lampara.
        if (estadobuzzer)
            printf("BUZZER Emergency\n");
        else
            printf("BUZZER Emergency\n");

        fflush(stdout); // Asegura impresión inmediata

        tiempoUltimoCambio = tiempoActual; // Actualiza el tiempo
    }
}
