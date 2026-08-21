#ifndef TIMER_H
#define TIMER_H

#include <time.h>

// Fonction générique qui mesure le temps d’exécution d’une fonction
// func : pointeur vers la fonction à exécuter
// arg  : argument passé à la fonction (ou NULL si aucun)
// return : temps en secondes (double)
double measure_time(void (*func)(void *), void *arg);

#endif
