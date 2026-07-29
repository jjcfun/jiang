#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

static atomic_int main_shutdown_completed;
static atomic_int main_shutdown_ran_on_main;

static void verify_main_shutdown(void) {
    if (atomic_load_explicit(
        &main_shutdown_completed,
        memory_order_acquire
    ) == 0) {
        _Exit(91);
    }
    if (atomic_load_explicit(
        &main_shutdown_ran_on_main,
        memory_order_acquire
    ) == 0) {
        _Exit(92);
    }
}

void main_shutdown_prepare(void) {
    atomic_store_explicit(
        &main_shutdown_completed,
        0,
        memory_order_relaxed
    );
    atomic_store_explicit(
        &main_shutdown_ran_on_main,
        0,
        memory_order_relaxed
    );
    atexit(verify_main_shutdown);
}

void main_shutdown_complete(void) {
    atomic_store_explicit(
        &main_shutdown_ran_on_main,
        pthread_main_np() != 0,
        memory_order_relaxed
    );
    atomic_store_explicit(
        &main_shutdown_completed,
        1,
        memory_order_release
    );
}
