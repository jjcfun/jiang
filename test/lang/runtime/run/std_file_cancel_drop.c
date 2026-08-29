#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

typedef void (*JiangResume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    JiangResume resume;
} JiangAsyncContinuation;

static _Atomic int64_t gate_ready;
static _Atomic int64_t gate_open;
static pthread_t pending_thread;
static JiangAsyncContinuation *pending_continuation;

static void *run_file_cancel_wait(void *opaque) {
    (void)opaque;
    atomic_store(&gate_ready, 1);
    while (atomic_load(&gate_open) == 0) {
        sched_yield();
    }
    *pending_continuation->result = 7;
    pending_continuation->resume(pending_continuation->context);
    return 0;
}

void file_cancel_wait(JiangAsyncContinuation *continuation) {
    pending_continuation = continuation;
    pthread_create(&pending_thread, 0, run_file_cancel_wait, 0);
}

void file_cancel_wait_ready(void) {
    while (atomic_load(&gate_ready) == 0) {
        sched_yield();
    }
}

static void *open_file_cancel(void *opaque) {
    (void)opaque;
    usleep(10000);
    atomic_store(&gate_open, 1);
    return 0;
}

void file_cancel_open_later(void) {
    pthread_t thread;
    if (pthread_create(&thread, 0, open_file_cancel, 0) == 0) {
        pthread_detach(thread);
    }
}

void file_cancel_join(void) {
    pthread_join(pending_thread, 0);
}

int64_t file_cancel_open_fd_count(void) {
    int64_t count = 0;
    long limit = sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 65536) {
        limit = 65536;
    }
    for (int fd = 0; fd < limit; fd += 1) {
        if (fcntl(fd, F_GETFD) != -1) {
            count += 1;
        }
    }
    return count;
}
