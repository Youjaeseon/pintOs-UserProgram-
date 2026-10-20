#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct fork_args {
    struct thread *parent;
    struct intr_frame user_frame;
    struct child_status *child_status;

    struct semaphore fork_done;
    bool fork_success;
};

struct initd_args {
    char *file_name;
    struct child_status *status;
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
struct child_status *process_find_child_status (tid_t child_tid);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
