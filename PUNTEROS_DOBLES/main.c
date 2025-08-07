#include <stdio.h>
#include <stdlib.h>

void crearUnArregloEntero(int *puntAInt, int dim);
int *crearUnArregloEntero2(int *puntAInt, int dim);
int crearUnArregloEntero3(int **puntAInt, int dim);
void mostrar(int a[], int val);  // Se declara aquí antes de main

int main()
{
    int *punt;
    int val = crearUnArregloEntero3(&punt, 10);
    mostrar(punt, val);
    free(punt); // Buena práctica: liberar memoria
    return 0;
}

int crearUnArregloEntero3(int **puntAInt, int dim)
{
    *puntAInt = (int *) malloc(dim * sizeof(int));
    if (*puntAInt == NULL) {
        printf("Error al asignar memoria.\n");
        exit(1);
    }

    int i;
    for (i = 0; i < dim; i++)
    {
        (*puntAInt)[i] = i;
    }
    return i;
}

void crearUnArregloEntero(int *puntAInt, int dim)
{
    puntAInt = (int *) malloc(dim * sizeof(int));
    if (puntAInt == NULL) {
        printf("Error al asignar memoria.\n");
        return;
    }

    int i;
    for (i = 0; i < dim; i++)
    {
        puntAInt[i] = i;
    }
    mostrar(puntAInt, dim);
    free(puntAInt); // Para evitar fugas de memoria
}

int *crearUnArregloEntero2(int *puntAInt, int dim)
{
    puntAInt = (int *) malloc(dim * sizeof(int));
    if (puntAInt == NULL) {
        printf("Error al asignar memoria.\n");
        return NULL;
    }

    int i;
    for (i = 0; i < dim; i++)
    {
        puntAInt[i] = i;
    }
    return puntAInt;
}

void mostrar(int a[], int val)
{
    int i;
    for (i = 0; i < val; i++)
    {
        printf("| %d |", a[i]);
    }
    printf("\n");
}
