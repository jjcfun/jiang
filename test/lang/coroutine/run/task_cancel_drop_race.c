#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t race_ready;
static _Atomic int64_t race_open;
static _Atomic int64_t drop_count;
static _Atomic int64_t iteration;
static pthread_t opener_thread;

void cancel_drop_race_registration_block(void) {
    atomic_store_explicit(&race_ready, 1, memory_order_release);
    while (atomic_load_explicit(&race_open, memory_order_acquire) == 0) {
        sched_yield();
    }
}

void cancel_drop_race_wait_ready(void) {
    while (atomic_load_explicit(&race_ready, memory_order_acquire) == 0) {
        sched_yield();
    }
}

static void *open_race(void *opaque) {
    (void)opaque;
    int64_t value = atomic_fetch_add_explicit(&iteration, 1, memory_order_relaxed);
    if ((value & 1) != 0) {
        usleep(1);
    } else {
        sched_yield();
    }
    atomic_store_explicit(&race_open, 1, memory_order_release);
    return 0;
}

void cancel_drop_race_open_later(void) {
    pthread_create(&opener_thread, 0, open_race, 0);
}

void cancel_drop_race_join_and_reset(void) {
    pthread_join(opener_thread, 0);
    atomic_store_explicit(&race_open, 0, memory_order_release);
    atomic_store_explicit(&race_ready, 0, memory_order_release);
}

void cancel_drop_race_record_drop(void) {
    atomic_fetch_add_explicit(&drop_count, 1, memory_order_relaxed);
}

int64_t cancel_drop_race_drop_count(void) {
    return atomic_load_explicit(&drop_count, memory_order_relaxed);
}
