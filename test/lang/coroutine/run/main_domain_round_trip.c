#include <pthread.h>
#include <stdint.h>

static pthread_t process_main_thread;

__attribute__((constructor))
static void capture_process_main_thread(void) {
    process_main_thread = pthread_self();
}

int64_t on_process_main_thread(void) {
    return pthread_equal(pthread_self(), process_main_thread) != 0;
}
