#include <stdint.h>

typedef void (*JiangResume)(void *);

extern void __jiang_domain_enqueue(int64_t domain_id, int64_t domain_kind, void *context, JiangResume resume);

static int64_t probe_state;

static void second_resume(void *context) {
    int64_t *state = context;
    *state = 2;
}

static void first_resume(void *context) {
    int64_t *state = context;
    *state = 1;
    __jiang_domain_enqueue(1, 0, context, second_resume);
    if (*state != 1) {
        *state = 99;
    }
}

int64_t run_domain_fifo_probe(void) {
    probe_state = 0;
    __jiang_domain_enqueue(1, 0, &probe_state, first_resume);
    return probe_state;
}
