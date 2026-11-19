#ifndef BENCH_CLOCK_H
#define BENCH_CLOCK_H

#ifdef _WIN32
# include <windows.h>
#else
# include <time.h>
#endif

static double bench_cpu(void) {
#ifdef _WIN32
    /* Win32 CPU time is quantum-granular: use the high-res wall clock */
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c), QueryPerformanceFrequency(&f);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

#endif /* BENCH_CLOCK_H */
