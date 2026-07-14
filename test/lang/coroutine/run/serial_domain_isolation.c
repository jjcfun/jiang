#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static _Atomic int64_t same_active;
static _Atomic int64_t distinct_entered;

int64_t enter_same_domain_probe(void) {
    atomic_fetch_add(&same_active, 1);
    usleep(100000);
    int64_t result = atomic_load(&same_active) == 1 ? 1 : 0;
    atomic_fetch_sub(&same_active, 1);
    return result;
}

int64_t enter_distinct_domain_probe(void) {
    atomic_fetch_add(&distinct_entered, 1);
    for (int index = 0; index < 2000; ++index) {
        if (atomic_load(&distinct_entered) >= 2) {
            return 1;
        }
        usleep(1000);
    }
    return 0;
}
