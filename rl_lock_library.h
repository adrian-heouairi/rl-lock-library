#ifndef _RL_LOCK_LIBRARY
#define _RL_LOCK_LIBRARY

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#define RL_MAX_MAP_ENTRIES 256
#define RL_MAX_OWNERS 32
#define RL_MAX_LOCKS 32
#define RL_MAX_FILES 256
#define RL_FREE_OWNER -1
#define RL_FREE_FILE NULL
#define RL_FREE_LOCK -2
#define SHM_PREFIX "f"

typedef struct rl_pid_fd_count rl_pid_fd_count;
typedef struct rl_owner rl_owner;
typedef struct rl_lock rl_lock;
typedef struct rl_open_file rl_open_file;
typedef struct rl_descriptor rl_descriptor;
typedef struct rl_all_files rl_all_files;

/**
 * @brief A map entry with key = PID and value = fd count
 */
struct rl_pid_fd_count {
    pid_t pid; /**< The PID of a process which has opened a specific file */
    int fd_count; /**< The number of times the process has opened the file */
};

/**
 * @brief The owner of a locked segment
 */
struct rl_owner {
    pid_t pid; /**< The PID of the process that locked a segment */
    int fd; /**< The file descriptor of the locked file */
};

/**
 * @brief The locked segment of a file
 */
struct rl_lock {
    off_t start; /**< The beginning of the segment */
    off_t len; /**< The length of the segment */
    short type; /**< The type (F_RDLCK, F_WRLCK) of the lock */
    size_t nb_owners; /**< The number of owners of the lock */
    rl_owner lock_owners[RL_MAX_OWNERS]; /**< The owners of the lock */
};

/**
 * @brief The locks on an open file description
 */
struct rl_open_file {
    int nb_locks; /**< The number of locks */
    pthread_mutex_t mutex; /**< The exclusive lock on the open file */
    rl_lock lock_table[RL_MAX_LOCKS]; /**< The locks on the open file */
    int nb_map_entries; /**< The number of entries in `pid_map` */
    rl_pid_fd_count pid_map[RL_MAX_MAP_ENTRIES]; /**< The map storing which
                                                  * processes have opened the
                                                  * file and how many times
                                                  */
};

/**
 * @brief The open file description
 */
struct rl_descriptor {
    int fd; /**< The open file descriptor as in the descriptor table */
    rl_open_file *file; /**< The locks on the open file */
};

/**
 * @brief All the open file descriptions of a process
 */
struct rl_all_files {
    int nb_files; /**< The number of open file descriptions */
    rl_open_file *open_files[RL_MAX_FILES]; /**< The open file descriptions */
};

rl_descriptor rl_open(const char *path, int oflag, ...);
int rl_close(rl_descriptor lfd);
int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);
rl_descriptor rl_dup(rl_descriptor lfd);
rl_descriptor rl_dup2(rl_descriptor lfd, int newd);
pid_t rl_fork();
int rl_init_library();

int rl_print_open_file(rl_open_file *file, int display_pids);
int rl_print_open_file_safe(rl_open_file *file, int display_pids);

#endif
