#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

extern bool __jiang_system_thread_pump_main_queue_once(void);

static pthread_t process_main_thread;

__attribute__((constructor))
static void capture_process_main_thread(void) {
    process_main_thread = pthread_self();
}

static void *run_non_main_pump(void *context) {
    int64_t *result = context;
    *result = __jiang_system_thread_pump_main_queue_once() ? 1 : 0;
    return NULL;
}

int64_t run_non_main_pump_probe(void) {
    pthread_t thread;
    int64_t result = 3;
    if (pthread_create(&thread, NULL, run_non_main_pump, &result) != 0) {
        return 4;
    }
    if (pthread_join(thread, NULL) != 0) {
        return 5;
    }
    return result;
}

int64_t on_process_main_thread(void) {
    return pthread_equal(pthread_self(), process_main_thread) != 0;
}
