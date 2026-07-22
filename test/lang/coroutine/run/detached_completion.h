#ifndef JIANG_TEST_DETACHED_COMPLETION_H
#define JIANG_TEST_DETACHED_COMPLETION_H

#include <limits.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>

#define JIANG_JOIN_IMPL(left, right) left##right
#define JIANG_JOIN(left, right) JIANG_JOIN_IMPL(left, right)

#define JIANG_DETACHED_COMPLETION(name)                                      \
    static _Atomic int64_t JIANG_JOIN(name, _result) = INT64_MIN;            \
    void JIANG_JOIN(name, _publish)(int64_t value) {                         \
        atomic_store_explicit(                                               \
            &JIANG_JOIN(name, _result), value, memory_order_release          \
        );                                                                   \
    }                                                                        \
    int64_t JIANG_JOIN(name, _wait)(void) {                                  \
        int64_t value;                                                       \
        do {                                                                 \
            value = atomic_load_explicit(                                    \
                &JIANG_JOIN(name, _result), memory_order_acquire             \
            );                                                               \
            if (value == INT64_MIN) { sched_yield(); }                       \
        } while (value == INT64_MIN);                                        \
        return value;                                                        \
    }

#endif
