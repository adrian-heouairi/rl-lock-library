#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "panic.h"
#include "rl_lock_library.h"

/*
 * Places a read lock, then places a write lock in the middle of the read lock,
 * which causes the read lock to be split in two parts. Then places a read lock
 * contiguous to the second part of the initial read lock, which causes the
 * merge of the read locks: r -> rwr -> (rwrr ->) rwR
 */

int main() {
    /* Initialize library */
    rl_init_library();

#define FILENAME "/tmp/test_write_lock_middle_read_lock.c"
    /* Create new file */
    rl_descriptor lfd = rl_open(FILENAME, O_CREAT | O_RDWR | O_TRUNC,
            S_IRUSR | S_IWUSR);
    if (lfd.fd < 0 || lfd.file == NULL)
        PANIC_EXIT("rl_open()");

    /* Make it 15 bytes long */
    if (ftruncate(lfd.fd, 15) < 0) {
        perror("ftruncate()");
        goto error;
    }

    printf("Placing read lock on [0; 9[\n");

    /* Place read lock on [0; 9[ */
    struct flock lck;
    lck.l_type = F_RDLCK;
    lck.l_whence = SEEK_END; // equivalent to (SEEK_SET, 0)
    lck.l_start = -15;
    lck.l_len = 9;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0) {
        fprintf(stderr, "rl_fcntl()\n");
        goto error;
    }

    if (rl_print_open_file_safe(lfd.file, 0) < 0) {
        fprintf(stderr, "rl_print_open_file_safe()\n");
        goto error;
    }

    printf("\nPlacing write lock on [3; 6[\n");

    /* Divide read lock in two parts by placing write lock on [3; 6[ */
    lck.l_type = F_WRLCK;
    lck.l_whence = SEEK_SET;
    lck.l_start = 3;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0) {
        fprintf(stderr, "rl_fcntl()\n");
        goto error;
    }

    if (rl_print_open_file_safe(lfd.file, 0) < 0) {
        fprintf(stderr, "rl_print_open_file_safe()\n");
        goto error;
    }

    printf("\nPlacing read lock on [9; 12[, causing merge\n");

    /* Merge read lock on [6; 9[ with new read lock on [9; 12[ */
    lck.l_type = F_RDLCK;
    lck.l_whence = SEEK_CUR;
    lck.l_start = 9;
    lck.l_len = 3;

    if (rl_fcntl(lfd, F_SETLK, &lck) < 0) {
        fprintf(stderr, "rl_fcntl()\n");
        goto error;
    }

    if (rl_print_open_file_safe(lfd.file, 0) < 0) {
        fprintf(stderr, "rl_print_open_file_safe()\n");
        goto error;
    }

    /* Close open file */
    if (rl_close(lfd) < 0) {
        fprintf(stderr, "rl_close()\n");
        goto error;
    }

    if (unlink(FILENAME) < 0) {
        perror("unlink()");
        goto error;
    }

    return 0;

 error:
    close(lfd.fd);
    unlink(FILENAME);
    return 1;
}
