#define _POSIX_C_SOURCE 199309L
#include "timer.h"
#include <stdio.h>
#include <time.h>


double measure_time(void (*func)(void *), void *arg) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    func(arg);

    clock_gettime(CLOCK_MONOTONIC, &end);

    long sec = end.tv_sec - start.tv_sec;
    long nsec = end.tv_nsec - start.tv_nsec;

    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000L;
    }

    return (double)sec + (double)nsec / 1e9;
}

