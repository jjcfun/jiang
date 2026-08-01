#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "Linux hosted ABI probe requires Linux x86_64"
#endif

_Static_assert(offsetof(struct dirent, d_name) == 19, "unexpected glibc dirent d_name offset");
_Static_assert(offsetof(struct stat, st_mtim) == 88, "unexpected glibc stat st_mtim offset");
_Static_assert(sizeof(struct stat) <= 144, "Jiang stat buffer is too small");
_Static_assert(sizeof(pthread_mutex_t) <= 64, "Jiang pthread mutex storage is too small");
_Static_assert(sizeof(pthread_cond_t) <= 64, "Jiang pthread condition storage is too small");
_Static_assert(sizeof(pthread_t) <= sizeof(uintptr_t), "Jiang pthread handle storage is too small");
_Static_assert(sizeof(pthread_key_t) <= sizeof(uint32_t), "Jiang pthread key storage is too small");
_Static_assert(sizeof(posix_spawn_file_actions_t) <= 80, "Jiang spawn file-actions storage is too small");
_Static_assert(_Alignof(posix_spawn_file_actions_t) <= 8, "Jiang spawn file-actions storage is under-aligned");

int main(void) {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    if (pthread_mutex_lock(&mutex) != 0) {
        return 1;
    }
    if (pthread_mutex_unlock(&mutex) != 0) {
        return 2;
    }
    void *process = dlopen(NULL, RTLD_NOW | RTLD_LOCAL);
    if (process == NULL) {
        return 3;
    }
    return dlclose(process) == 0 ? 0 : 4;
}
