#include "panic.h"
#include "rl_lock_library.h"
#include <stdio.h>
#include <unistd.h>

/*
 * This test opens a file, places a lock on it and calls rl_dup() and rl_dup2()
 * on the rl_descriptor. Then we unlock the middle of the region for the
 * first rl_descriptor, and we can see that only two owners are left for the
 * first lock, and two new locks appear.
 */

int main() {
    unlink("/tmp/test_rl_dup");

    rl_init_library();

    rl_descriptor lfd = rl_open("/tmp/test_rl_dup", O_RDWR | O_CREAT, 0777);
    if (lfd.fd == -1) PANIC_EXIT("rl_open");
    struct flock l;
    l.l_start = 3;
    l.l_len = 4;
    l.l_whence = SEEK_SET;
    l.l_type = F_WRLCK;
    if (rl_fcntl(lfd, F_SETLK, &l))
        PANIC_EXIT("rl_fcntl");
    
    printf("Before dup():\n");
    rl_print_open_file_safe(lfd.file, 1);
    printf("\n");

    rl_descriptor new_fd = rl_dup(lfd);
    if (new_fd.fd == -1) PANIC_EXIT("rl_dup");

    printf("After dup():\n");
    rl_print_open_file_safe(lfd.file, 1);
    printf("\n");

    rl_descriptor new_fd2 = rl_dup2(lfd, 15);
    if (new_fd2.fd == -1) PANIC_EXIT("rl_dup2");

    printf("After dup2():\n");
    rl_print_open_file_safe(lfd.file, 1);
    printf("\n");

    l.l_type = F_UNLCK;
    l.l_start = 4;
    l.l_len = 1;
    if (rl_fcntl(lfd, F_SETLK, &l))
        PANIC_EXIT("rl_fcntl");

    printf("After unlock in the middle on fd 3:\n");
    rl_print_open_file_safe(lfd.file, 1);

    if (rl_close(lfd) < 0)
        return -1;

    if (rl_close(new_fd) < 0)
        return -1;

    if (rl_close(new_fd2) < 0)
        return -1;
}
