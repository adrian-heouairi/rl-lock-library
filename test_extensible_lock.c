#include <stdio.h>

#include "panic.h"
#include "rl_lock_library.h"

/*
 * Creates a file where 3 uncontiguous read locks are placed, then unlocks the
 * entirety of the file with an extensible lock, then places an extensible write
 * lock on [10; +inf[ and finally divides in two parts by putting a read lock
 * on [15; 20[. The result should be a finite write lock on [0; 15[, a finite
 * read lock on [15; 20[ and an extensible write lock on [20; +inf[.
 */

int main() {
#define FILENAME "/tmp/test-extensible-lock.txt"
    rl_init_library();

    rl_descriptor lfd = rl_open(FILENAME, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (lfd.fd < 0 || lfd.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("Opened file\n");

    struct flock lck;
    lck.l_type = F_RDLCK;
    lck.l_whence = SEEK_SET;
    lck.l_start = 1;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [1; 4[\n");

    lck.l_start = 5;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [5; 8[\n");

    lck.l_start = 9;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [9; 12[\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");

    lck.l_type = F_UNLCK;
    lck.l_start = 0;
    lck.l_len = 0;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Unlocked entire file\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");

    lck.l_type = F_WRLCK;
    lck.l_start = 10;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed extensible write lock on at 10\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");

    lck.l_type = F_RDLCK;
    lck.l_start = 15;
    lck.l_len = 5;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Placed read lock on [15; 20[\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");

    if (rl_close(lfd) < 0)
        PANIC_EXIT("rl_close()");

    printf("Closed locked file description\n");

    if (rl_print_open_file_safe(lfd.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file()");

    if (unlink(FILENAME) < 0)
        PANIC_EXIT("unlink()");

    return 0;
}
