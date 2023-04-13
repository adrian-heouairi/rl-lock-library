/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#include "rl_lock_library.h"
#define NB_OWNERS 32
#define NB_LOCKS 32
#define NB_FILES 256

struct rl_owner {
    pid_t pid;
    int fd;
};

struct rl_lock {
    int next_lock;
    off_t starting_offset;
    off_t len;
    short type; /* F_RDLCK, F_WRLCK */
    size_t nb_owners;
    rl_owner lock_owners[NB_OWNERS];
};

struct rl_open_file {
    int first;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rl_lock lock_table[NB_LOCKS];
};

struct rl_descriptor {
    int fd;
    rl_open_file *of;
};

struct rl_all_files {
    int nb_files;
    rl_open_file *open_files[NB_FILES];
};

/*
 * Initializes a pthread mutex for process synchronization.
 */
static int initialize_mutex(pthread_mutex_t *pmutex) {
    pthread_mutexattr_t mutexattr;
    int code;

    code = pthread_mutexattr_init(&mutexattr);
    if (code != 0)
        return code;
    code = pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    if(code != 0)
        return code;
    return pthread_mutex_init(pmutex, &mutexattr);
}

/*
 * Initializes a pthread cond for process synchronization.
 */
static int initialize_cond(pthread_cond_t *pcond) {
    pthread_condattr_t condattr;
    int code;

    code = pthread_condattr_init(&condattr);
    if(code != 0)
        return code;
    code = pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    if (code != 0)
        return code;
    return pthread_cond_init(pcond, &condattr);
}

/*
 * If lock is not NULL, moves the owners in lock->lock_owners in order to fit in
 * the lock->nb_owners first cells. If lock->nb_owners is strictly inferior to
 * 0 or strictly superior to NB_OWNERS, or lock is NULL, returns -1.
 * If lock->nb_owners is superior to the real number of owners, returns -1. If
 * lock->nb_owners is less then the real number of owners (but superior to 0),
 * only the first lock->nb_owners owners will move in order to fit the first
 * cells, leaving the rest unchanged. On success and in the latter case, returns
 * 0.
 */
static int organize_owners(rl_lock *lock) {
    int i, j;

    if (lock == NULL || lock->nb_owners < 0 || lock->nb_owners > NB_OWNERS)
        return -1;
    for (i = 0; i < lock->nb_owners; i++) {
        if (lock->lock_owners[i].fd == -1) {
            j = i + 1;
            while (j < NB_OWNERS && lock->lock_owners[j].fd == -1)
                j++;
            if (j >= NB_OWNERS)
                return -1;
            lock->lock_owners[i] = lock->lock_owners[j];
            lock->lock_owners[j].fd = -1;
        }
    }
    return 0;
}
