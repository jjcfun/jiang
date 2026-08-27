#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

typedef void (*runtime_gate_callback)(intptr_t);

static _Atomic int64_t runtime_gate_active;
static _Atomic int64_t runtime_gate_destroyed;

void runtime_gate_submit(
    intptr_t context,
    runtime_gate_callback callback
) {
    dispatch_async_f(
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
        (void *)context,
        (dispatch_function_t)callback
    );
}

int64_t runtime_gate_probe(void) {
    int64_t previous = atomic_fetch_add(&runtime_gate_active, 1);
    usleep(20000);
    atomic_fetch_sub(&runtime_gate_active, 1);
    return previous == 0 ? 0 : 1;
}

void runtime_gate_record_destroy(void) {
    atomic_fetch_add(&runtime_gate_destroyed, 1);
}

void runtime_gate_wait_destroy(void) {
    for (int index = 0; index < 5000; ++index) {
        if (atomic_load(&runtime_gate_destroyed) != 0) {
            return;
        }
        usleep(1000);
    }
}

int64_t runtime_gate_destroy_count(void) {
    return atomic_load(&runtime_gate_destroyed);
}
