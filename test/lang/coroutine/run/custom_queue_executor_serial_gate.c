#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*queue_callback)(intptr_t);

static _Atomic int64_t active[2];
static _Atomic int64_t distinct_entered;
static _Atomic int64_t created;
static _Atomic int64_t destroyed;

void queue_executor_submit(intptr_t context, queue_callback callback) {
    dispatch_async_f(
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
        (void *)context,
        (dispatch_function_t)callback
    );
}

void queue_executor_created(void) {
    atomic_fetch_add(&created, 1);
}

void queue_executor_destroyed(void) {
    atomic_fetch_add(&destroyed, 1);
}

int64_t serial_domain_probe(int64_t domain) {
    int64_t previous = atomic_fetch_add(&active[domain], 1);
    usleep(20000);
    atomic_fetch_sub(&active[domain], 1);
    return previous == 0 ? 0 : 1;
}

int64_t distinct_domain_probe(int64_t domain) {
    (void)domain;
    atomic_fetch_add(&distinct_entered, 1);
    for (int index = 0; index < 2000; ++index) {
        if (atomic_load(&distinct_entered) >= 2) {
            return 1;
        }
        usleep(1000);
    }
    return 0;
}

__attribute__((destructor))
static void verify_executor_lifecycle(void) {
    if (atomic_load(&created) != 2 || atomic_load(&destroyed) != 2) {
        abort();
    }
}
