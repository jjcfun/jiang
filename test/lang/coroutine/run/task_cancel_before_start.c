#include <sched.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

static _Atomic int64_t gate_ready;
static _Atomic int64_t gate_open;
static _Atomic int64_t drop_count;
static _Atomic int64_t drop_sum;

void cancel_gate_record_drop(int64_t value) {
    atomic_fetch_add(&drop_count, 1);
    atomic_fetch_add(&drop_sum, value);
}

int64_t cancel_gate_drop_count(void) {
    return atomic_load(&drop_count);
}

int64_t cancel_gate_drop_sum(void) {
    return atomic_load(&drop_sum);
}

int64_t cancel_gate_check_drops(int64_t expected_count, int64_t expected_sum) {
    int64_t count = atomic_load(&drop_count);
    int64_t sum = atomic_load(&drop_sum);
    if (count == expected_count && sum == expected_sum) { return 1; }
    fprintf(stderr, "capture drops=%lld sum=%lld, expected drops=%lld sum=%lld\n",
        (long long)count, (long long)sum, (long long)expected_count, (long long)expected_sum);
    return 0;
}

void cancel_gate_block(void) {
    atomic_store(&gate_ready, 1);
    while (atomic_load(&gate_open) == 0) {
        sched_yield();
    }
}

void cancel_gate_wait_ready(void) {
    while (atomic_load(&gate_ready) == 0) {
        sched_yield();
    }
}

static void *cancel_gate_open_thread(void *unused) {
    usleep(10000);
    atomic_store(&gate_open, 1);
    return unused;
}

void cancel_gate_open_later(void) {
    pthread_t thread;
    if (pthread_create(&thread, 0, cancel_gate_open_thread, 0) == 0) {
        pthread_detach(thread);
    }
}
