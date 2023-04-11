/*
 * Adrian HEOUAIRI
 * Guillermo MORON USON
 */

#include "rl_lock_library.h"
#define NB_OWNERS 32
#define NB_LOCKS 32
#define NB_FILES 256

struct rl_owner
{
    pid_t pid;
    int fd;
};

struct rl_lock
{
    int next_lock;
    off_t starting_offset;
    off_t len;
    short type; /* F_RDLCK, F_WRLCK */
    size_t nb_owners;
    rl_owner lock_owners[NB_OWNERS];
};

struct rl_open_file
{
    int first;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rl_lock lock_table[NB_LOCKS];
};

struct rl_descriptor
{
    int fd;
    rl_open_file *of;
};

struct rl_all_files
{
    int nb_files;
    rl_open_file *open_files[NB_FILES];
};

/*
 * Initializes a pthread mutex for process synchronization.
 */
static int initialize_mutex(pthread_mutex_t *pmutex)
{
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
static int initialize_cond(pthread_cond_t *pcond)
{
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
