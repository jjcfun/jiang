#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static _Atomic int64_t active;
static _Atomic int64_t peak;
static _Atomic int64_t coordinator_active;
static _Atomic int64_t coordinator_peak;
static _Atomic int shutdown_completed;

static void verify_compiler_domain_shutdown(void) {
    if (atomic_load_explicit(&shutdown_completed, memory_order_acquire) == 0) {
        _Exit(91);
    }
}

int64_t enter_compiler_domain_probe(void) {
    int64_t current = atomic_fetch_add_explicit(&active, 1, memory_order_relaxed) + 1;
    int64_t observed = atomic_load_explicit(&peak, memory_order_relaxed);
    while (observed < current && !atomic_compare_exchange_weak_explicit(
        &peak, &observed, current, memory_order_relaxed, memory_order_relaxed
    )) {}
    usleep(10000);
    atomic_fetch_sub_explicit(&active, 1, memory_order_relaxed);
    return 0;
}

int64_t compiler_domain_peak(void) {
    return atomic_load_explicit(&peak, memory_order_relaxed);
}

int64_t enter_coordinator_domain_probe(void) {
    int64_t current = atomic_fetch_add_explicit(
        &coordinator_active, 1, memory_order_relaxed
    ) + 1;
    int64_t observed = atomic_load_explicit(&coordinator_peak, memory_order_relaxed);
    while (observed < current && !atomic_compare_exchange_weak_explicit(
        &coordinator_peak, &observed, current, memory_order_relaxed, memory_order_relaxed
    )) {}
    usleep(10000);
    atomic_fetch_sub_explicit(&coordinator_active, 1, memory_order_relaxed);
    return 0;
}

int64_t coordinator_domain_peak(void) {
    return atomic_load_explicit(&coordinator_peak, memory_order_relaxed);
}

void prepare_compiler_domain_shutdown_probe(void) {
    atomic_store_explicit(&shutdown_completed, 0, memory_order_relaxed);
    atexit(verify_compiler_domain_shutdown);
}

void complete_compiler_domain_shutdown_probe(void) {
    usleep(10000);
    atomic_store_explicit(&shutdown_completed, 1, memory_order_release);
}
