#include <stdio.h>
#include <stdlib.h>

int main(void)
{

    //variables
    int **matriz, i,j, nfilas, ncols;
    nfilas = 5;
    ncols =10 ;
    //memoria dinamica
    matriz = (int **) calloc(nfilas,sizeof(int *));
    for (i= 0; i < nfilas; ++i)
    matriz[i] = (int *) calloc(ncols, sizeof(int));
    //asignacion de valores
    for (i = 0; i < nfilas; ++i)
        for(j = 0; j <ncols; ++j)
            matriz[i][j]=i+j;
        // salida por pantalla
      for (i = 0; i < nfilas; ++i){
        for(j = 0; j <ncols; ++j){
            printf("%d",matriz[i][j]);
            }
            printf("\n");
            }
            for (i =0; i < nfilas; ++i)
            free(matriz[i]);
            free(matriz);
    return 0;
}
