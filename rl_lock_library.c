/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#include "rl_lock_library.h"
#define RL_MAX_OWNERS 32
#define RL_MAX_LOCKS 32
#define RL_MAX_FILES 256
#define RL_FREE_OWNER -1
#define RL_FREE_FILE NULL
#define RL_NO_NEXT_LOCK -1
#define RL_FREE_LOCK -2
#define RL_NO_LOCKS -2

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
    rl_owner lock_owners[RL_MAX_OWNERS];
};

struct rl_open_file {
    int first;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rl_lock lock_table[RL_MAX_LOCKS];
};

struct rl_descriptor {
    int fd;
    rl_open_file *of;
};

struct rl_all_files {
    int nb_files;
    rl_open_file *open_files[RL_MAX_FILES];
};

/******************************************************************************/

/*
 * Initializes pmutex for process synchronization.
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
 * Initializes pcond for process synchronization.
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

/******************************************************************************/

/*
 * Returns 1 if owner is free, 0 otherwise. An owner is free if owner->fd equals
 * RL_FREE_OWNER.
 */
static int is_owner_free(rl_owner *owner) {
    return owner->fd == RL_FREE_OWNER;
}

/*
 * Erases the given owner. If owner is NULL, does nothing.
 */
static void erase_owner(rl_owner *owner) {
    if (owner != NULL) {
        owner->pid = (pid_t) RL_FREE_OWNER;
        owner->fd = RL_FREE_OWNER;
    }
}

/******************************************************************************/

/*
 * If lock is not NULL, moves the owners in lock->lock_owners in order to fit in
 * the lock->nb_owners first cells. If lock->nb_owners is strictly inferior to
 * 0 or strictly superior to RL_MAX_OWNERS, or lock is NULL, returns -1.
 * If lock->nb_owners is superior to the real number of owners, returns -1. If
 * lock->nb_owners is less then the real number of owners (but superior to 0),
 * only the first lock->nb_owners owners will move in order to fit the first
 * cells, leaving the rest unchanged. On success and in the latter case, returns
 * 0.
 */
static int organize_owners(rl_lock *lock) {
    int i, j;

    if (lock == NULL || lock->nb_owners < 0 || lock->nb_owners > RL_MAX_OWNERS)
        return -1;
    for (i = 0; i < lock->nb_owners; i++) {
        if (is_owner_free(&lock->lock_owners[i])) {
            j = i + 1;
            while (j < RL_MAX_OWNERS && is_owner_free(&lock->lock_owners[j]))
                j++;
            if (j >= RL_MAX_OWNERS)
                return -1;
            lock->lock_owners[i] = lock->lock_owners[j];
            erase_owner(&lock->lock_owners[j]);
        }
    }
    return 0;
}
