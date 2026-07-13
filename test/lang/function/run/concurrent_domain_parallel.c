#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t entered;

int64_t enter_parallel_probe(void) {
    atomic_fetch_add(&entered, 1);
    for (int index = 0; index < 2000; ++index) {
        if (atomic_load(&entered) >= 2) {
            return 1;
        }
        usleep(1000);
    }
    return 0;
}
