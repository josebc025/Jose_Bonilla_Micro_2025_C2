#include <stdio.h>
#include <stdlib.h>
enum Daidelasemana{
    DOM,
    LUN,
    MAR,
    MIE,
    JUE,
    VIE,
    SAB
};


int main()
{  int x;
    x = LUN;
   switch (x) {
   case DOM:
    printf("DOMINGO\n");
    break;
     case LUN:
    printf("LUNES\n");
    break;
     case MAR:
    printf("MARTES\n");
    break;
     case MIE:
    printf("MIERCOLES\n");
    break;
     case JUE:
    printf("JUEVES\n");
    break;
     case VIE:
    printf("VIERNES\n");
    break; case SAB:
    printf("SABADO\n");
    break;



   }
    return 0;
}
