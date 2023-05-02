/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#include "rl_lock_library.h"
#include <asm-generic/errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#define RL_MAX_OWNERS 32
#define RL_MAX_LOCKS 32
#define RL_MAX_FILES 256
#define RL_FREE_OWNER -1
#define RL_FREE_FILE NULL
#define RL_NO_NEXT_LOCK -1
#define RL_FREE_LOCK -2
#define RL_NO_LOCKS -2
#define SHM_PREFIX "f"

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

/**
 * The rla.nb_files first cells of the rla.open_files array contain pointers to
 * the rl_open_file corresponding to all files that have been open with rl_open
 * by this process.
 */
static rl_all_files rla;

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

/*
 * Returns 1 if o1.pid == o2.pid && o1.fd == o2.fd, 0 otherwise.
 */
static int equals(rl_owner o1, rl_owner o2) {
    return o1.pid == o2.pid && o1.fd == o2.fd;
}

/******************************************************************************/

/*
 * Returns 1 if lock is the last lock, that is if lock is not NULL and
 * lock->next_lock == RL_NO_NEXT_LOCK.
 */
static int is_last(rl_lock *lock) {
    return lock != NULL && lock->next_lock == RL_NO_NEXT_LOCK;
}

/*
 * rl_close() closes lfd.fd with close() and deletes all the locks associated to
 * that file descriptor and the calling process by erasing the corresponding
 * owner from the lock owners table of each concerned lock. If the corresponding
 * owner is the last remaining owner of a lock, the lock is also removed from
 * the lock table of the open file. If the call to close() on lfd.fd fails,
 * returns -1, thus leaving the lock table unmodified. If 
 */
int rl_close(rl_descriptor lfd) {
    rl_owner lfd_owner;
    rl_open_file *of;
    rl_lock *cur, *prev;
    int i, lock_owners_count, err;

    if (close(lfd.fd) == -1) {
        perror("close()");
        return -1;
    }

    lfd_owner = (rl_owner) {.pid = getpid(), .fd = lfd.fd};
    of = lfd.of;
    err = pthread_mutex_lock(&of->mutex);
    if (err != 0) {
        fprintf(stderr, "%s\n", strerror(err));
        return -1;
    }

    if (of->first == RL_NO_LOCKS) /* nothing to do */
        return 0;

    cur = &of->lock_table[of->first];
    prev = cur;
    while (1) {
        /* count owners after erase of lfd_owner */
        lock_owners_count = 0;
        for (i = 0; i < cur->nb_owners; i++) {
            if (equals(cur->lock_owners[i], lfd_owner))
                erase_owner(&cur->lock_owners[i]);
            else
                lock_owners_count++;
        }
        
        /* organize owners to fit in nb_owners first cells */
        if (lock_owners_count != 0) {
            cur->nb_owners = lock_owners_count;
            organize_owners(cur);
        }

        if (is_last(cur)) {
            if (lock_owners_count == 0) {
                prev->next_lock = RL_NO_NEXT_LOCK;
                cur->next_lock = RL_FREE_LOCK;
                if (cur == prev) /* cur is last and first */
                    of->first = RL_NO_LOCKS;
            }
            break;
        } else {
            if (lock_owners_count == 0) {
                /* cur is first and has next, next becomes new first */
                if (cur == &of->lock_table[of->first]){
                    of->first = cur->next_lock;
                    cur = &of->lock_table[of->first];
                    prev->next_lock = RL_FREE_LOCK;
                    prev = cur;
                } else { /* cur is neither first nor last */
                    prev->next_lock = cur->next_lock;
                    cur->next_lock = RL_FREE_LOCK;
                    cur = &of->lock_table[prev->next_lock];
                }
            } else {
                prev = cur;
                cur = &of->lock_table[prev->next_lock];
            }
        }
    }

    err = pthread_mutex_unlock(&of->mutex);
    if (err != 0) {
        fprintf(stderr, "%s\n", strerror(err));
        return -1;
    }
    return 0;
}

/******************************************************************************/

/**
 * Initializes rla, the static global variable recording all open files. You
 * must call this function before using the rl library.
 * @return always 0
 */
int rl_init_library() {
    rla.nb_files = 0;
    for (int i = 0; i < RL_MAX_FILES; i++)
        rla.open_files[i] = RL_FREE_FILE;
    
    return 0;
}

/******************************************************************************/

/**
 * Adds rlo to rla. Fails if rla is full. Does not set errno on error.
 * @return 0 on success, -1 on error
 */
static int add_to_rla(rl_open_file *rlo) {
    if (rla.nb_files >= RL_MAX_FILES)
        return -1;
    rla.open_files[rla.nb_files] = rlo;
    rla.nb_files++;
    return 0;
}

rl_descriptor rl_open(const char *path, int oflag, ...) {
    va_list va;
    va_start(va, oflag);

    rl_descriptor err_desc = {.fd = -1, .of = NULL};

    if (rla.nb_files >= RL_MAX_FILES) {
        errno = EMFILE;
        return err_desc;
    }
    
    int open_res;
    if (oflag & O_CREAT)
        open_res = open(path, oflag, va_arg(va, mode_t));
    else
        open_res = open(path, oflag);
    if (open_res == -1)
        return err_desc;
    
    struct stat st;
    int fstat_res = fstat(open_res, &st);
    if (fstat_res == -1) {
        close(open_res);
        return err_desc;
    }

    char shm_path[256];
    int snprintf_res = snprintf(shm_path, 256, "/%s_%lu_%lu", SHM_PREFIX,
        st.st_dev, st.st_ino);
    if (snprintf_res < 0) {
        close(open_res);
        errno = ECANCELED;
        return err_desc;
    }

    int shm_res = shm_open(shm_path, O_RDWR, 0);
    rl_open_file *rlo = NULL;
    // Problem: process 1 creates the shm and is paused before initializing it
    // then process 2 comes here and the shm exists but is not initialized
    if (shm_res >= 0) {
        rlo = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED,
            shm_res, 0);
        if (rlo == MAP_FAILED) {
            close(open_res);
            close(shm_res);
            return err_desc;
        }
    } else { // We create the shm
        int shm_res2 = shm_open(shm_path, O_RDWR | O_CREAT,
            S_IRWXU | S_IRWXG | S_IRWXO);
        if (shm_res2 == -1) {
            close(open_res);
            shm_unlink(shm_path);
            return err_desc;
        }
        
        int trunc_res = ftruncate(shm_res2, sizeof(rl_open_file));
        if (trunc_res == -1) {
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }

        rlo = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED,
            shm_res2, 0);
        if (rlo == MAP_FAILED) {
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }

        if (initialize_mutex(&rlo->mutex)) {
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }
        if (initialize_cond(&rlo->cond)) {
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }
        if (pthread_mutex_lock(&rlo->mutex)) {
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }

        rlo->first = RL_NO_LOCKS;
        for (int i = 0; i < RL_MAX_LOCKS; i++) {
            rlo->lock_table[i].next_lock = RL_FREE_LOCK;
            for (int j = 0; j < RL_MAX_OWNERS; j++)
                erase_owner(&rlo->lock_table[i].lock_owners[j]);
        }

        if (pthread_mutex_unlock(&rlo->mutex)) {
            close(open_res);
            close(shm_res2);
            shm_unlink(shm_path);
            return err_desc;
        }

        if (add_to_rla(rlo) == -1) {
            close(open_res);
            close(shm_res2);
            return err_desc;
        }
    }

    va_end(va);

    rl_descriptor desc = {.fd = open_res, .of = rlo};
    return desc;
}
