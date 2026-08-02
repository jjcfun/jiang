#include <unistd.h>

int jiang_test_create_dangling_symlink(void) {
    const char *path = "/tmp/jiang_hosted_fs_semantics/dangling";
    unlink(path);
    return symlink("missing-target", path);
}
