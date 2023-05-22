#include <stdio.h>

#include "panic.h"
#include "rl_lock_library.h"

/*
 * Shows that the problem of POSIX locks (locks drop for every descriptor when
 * one is closed) is solved by the library by showing that the locks placed
 * with the first descriptor are still in place when the second descriptor is
 * closed.
 */

int main() {
    rl_init_library();

    struct flock lck;
    lck.l_type = F_RDLCK;
    lck.l_whence = SEEK_SET;
    lck.l_start = 0;
    lck.l_len = 5;

#define FILENAME "/tmp/test-posix-problem-solved.txt"
    rl_descriptor lfd1 = rl_open(FILENAME, O_CREAT | O_RDONLY | O_TRUNC, 0644);
    if (lfd1.fd < 0 || lfd1.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("Succesfully opened file\n");

    if (rl_fcntl(lfd1, F_SETLK, &lck) < 0)
        PANIC_EXIT("rl_fcntl()");

    printf("Succesfully placed read lock\n");

    if (rl_print_open_file_safe(lfd1.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file_safe()");

    rl_descriptor lfd2 = rl_open(FILENAME, O_RDONLY);
    if (lfd2.fd < 0 || lfd2.file == NULL)
        PANIC_EXIT("rl_open()");

    printf("\nSuccesfully opened file, again\n");

    if (rl_close(lfd2) < 0)
        PANIC_EXIT("rl_close()");

    printf("Succesfully closed second file description\n");

    if (rl_print_open_file_safe(lfd1.file, 0) < 0)
        PANIC_EXIT("rl_print_open_file_safe()");

    if (rl_close(lfd1) < 0)
        PANIC_EXIT("rl_close()");

    printf("Succesfully closed first file description\n");

    return 0;
}
