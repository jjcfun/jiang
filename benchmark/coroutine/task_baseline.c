#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *case_name(int64_t case_id) {
    switch (case_id) {
        case 0: return "synchronous";
        case 1: return "same-domain";
        case 2: return "scoped-task";
        case 3: return "heap-owner";
        case 4: return "cross-domain";
        case 5: return "global-enqueue";
        case 6: return "main-enqueue";
        case 7: return "custom-enqueue";
        case 8: return "immediate-resume";
        case 9: return "cancel-completed";
        case 10: return "many-domains";
        case 11: return "runtime-inline";
        case 12: return "runtime-lifecycle";
        case 13: return "cancel-queued";
        case 14: return "cancel-suspended";
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

__attribute__((noinline))
int64_t coroutine_benchmark_sync_barrier(int64_t value) {
    __asm__ volatile("" : "+r"(value));
    return value;
}

void coroutine_benchmark_report(
    int64_t case_id,
    int64_t iterations,
    int64_t elapsed_ns,
    int64_t checksum,
    int64_t job_allocations
) {
    (void)job_allocations; // 未接入分配计数时不输出误导性的常量。
    double ns_per_op = (double)elapsed_ns / (double)iterations;
    printf(
        "%-20s iterations=%lld elapsed_ns=%lld ns/op=%.2f checksum=%lld\n",
        case_name(case_id),
        (long long)iterations,
        (long long)elapsed_ns,
        ns_per_op,
        (long long)checksum
    );
}
