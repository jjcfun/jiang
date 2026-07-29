#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*jiang_resume)(void *);

typedef struct {
    int64_t *result;
    void *context;
    jiang_resume resume;
} jiang_async_continuation;

typedef struct {
    jiang_async_continuation *continuation;
    int64_t value;
} resume_main_payload;

int64_t on_resume_main_thread(void) {
    return pthread_main_np() != 0;
}

static void *run_resume_main(void *opaque) {
    resume_main_payload *payload = opaque;
    usleep(10000);
    *payload->continuation->result = payload->value;
    payload->continuation->resume(payload->continuation->context);
    free(payload);
    return 0;
}

void resume_main_later(
    int64_t value,
    jiang_async_continuation *continuation
) {
    resume_main_payload *payload = malloc(sizeof(*payload));
    if (payload == NULL) {
        abort();
    }
    payload->continuation = continuation;
    payload->value = value;
    pthread_t thread;
    if (pthread_create(&thread, NULL, run_resume_main, payload) != 0) {
        abort();
    }
    pthread_detach(thread);
}
