#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t completion_count;

int64_t cancel_completion_jitter(void) {
    int64_t value = atomic_fetch_add(&completion_count, 1);
    switch (value & 3) {
        case 0:
            sched_yield();
            break;
        case 1:
            usleep(1);
            break;
        case 2:
            usleep(20);
            break;
        default:
            break;
    }
    return value;
}

int64_t cancel_completion_count(void) {
    return atomic_load(&completion_count);
}
