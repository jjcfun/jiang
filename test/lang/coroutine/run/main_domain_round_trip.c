#include <pthread.h>
#include <stdint.h>

int64_t on_process_main_thread(void) {
    return pthread_main_np() != 0;
}
