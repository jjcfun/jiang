#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *case_name(int64_t case_id) {
    switch (case_id) {
        case 0: return "direct";
        case 1: return "scoped-task";
        case 2: return "heap-owner";
        case 3: return "cross-domain";
        case 4: return "immediate-resume";
        case 5: return "cancel-before-start";
        case 6: return "many-domains";
        default: return "unknown";
    }
}

int64_t coroutine_benchmark_iterations(void) {
    const char *text = getenv("JIANG_BENCH_ITERATIONS");
    if (text == NULL || *text == '\0') {
        return 10000;
    }
    errno = 0;
    char *end = NULL;
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0) {
        fprintf(stderr, "invalid JIANG_BENCH_ITERATIONS: %s\n", text);
        exit(2);
    }
    return (int64_t)value;
}

int64_t coroutine_benchmark_now_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
}

void coroutine_benchmark_report(
    int64_t case_id,
    int64_t iterations,
    int64_t elapsed_ns,
    int64_t checksum
) {
    double ns_per_op = (double)elapsed_ns / (double)iterations;
    printf(
        "%-12s iterations=%lld elapsed_ns=%lld ns/op=%.2f checksum=%lld\n",
        case_name(case_id),
        (long long)iterations,
        (long long)elapsed_ns,
        ns_per_op,
        (long long)checksum
    );
}
