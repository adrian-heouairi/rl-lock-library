/*
 * HEOUAIRI
 * MORON USON
 */

#include "rl_lock_library.h"
#define NB_OWNERS 8
#define NB_LOCKS 8
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
