#include <stdatomic.h>
#include <stdint.h>
#include <sched.h>
#include <unistd.h>

static _Atomic int64_t sequence;

int64_t completion_jitter(void) {
    int64_t value = atomic_fetch_add(&sequence, 1);
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
    return 1;
}
