#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t ready_bits;
static _Atomic int64_t drop_sum;

void checkpoint_mark_ready(int64_t bit) {
    atomic_fetch_or_explicit(&ready_bits, bit, memory_order_release);
}

int64_t checkpoint_wait_ready(int64_t bits) {
    for (int index = 0; index < 500; ++index) {
        int64_t observed = atomic_load_explicit(&ready_bits, memory_order_acquire);
        if ((observed & bits) == bits) {
            return observed;
        }
        usleep(10000);
    }
    return atomic_load_explicit(&ready_bits, memory_order_acquire);
}

void checkpoint_add_drop(int64_t value) {
    atomic_fetch_add_explicit(&drop_sum, value, memory_order_release);
}

int64_t checkpoint_wait_drops(int64_t value) {
    for (int index = 0; index < 500; ++index) {
        int64_t observed = atomic_load_explicit(&drop_sum, memory_order_acquire);
        if (observed == value) {
            return observed;
        }
        usleep(10000);
    }
    return atomic_load_explicit(&drop_sum, memory_order_acquire);
}
