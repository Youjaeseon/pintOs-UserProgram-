#include "userprog/syscall.h"
#include <stdio.h>
#include <stdint.h> // 주소 계산에 사용.
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <string.h>
#include "threads/init.h" // 컴파일 전 헤더 내용을 현재 파일에 포함.
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include <stdbool.h>
#include <stddef.h>
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
bool vaild_addr(uint64_t addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    lock_init(&filesys_lock);
}

static void
terminate_process (int status) {
    // printf ("%s: exit(%d)\n", thread_name (), status);
    // thread_exit ();
    thread_current()->exit_status = status;
    thread_exit();
}

static bool
valid_read_buffer (const void *buffer, unsigned size) {
    if (size == 0)
        return true;

    uintptr_t address = (uintptr_t) buffer; // 주소 값을 정수로 담기.

    if (buffer == NULL || size - 1 > UINTPTR_MAX - address)
        return false;

    uintptr_t last = address + size - 1;

    if (!is_user_vaddr ((void *) address)
        || !is_user_vaddr ((void *) last))
        return false;

    unsigned remaining = size;

    while (remaining > 0) {
        if (pml4_get_page (thread_current ()->pml4,
                          (void *) address) == NULL)
            return false;

        unsigned chunk = PGSIZE - pg_ofs ((void *) address);

        if (chunk >= remaining)
            break;

        address += chunk;
        remaining -= chunk;
    }

    return true;
}

bool vaild_addr(uint64_t addr)
{
    if (addr == NULL || !is_user_vaddr (addr) || pml4_get_page (thread_current ()->pml4, addr) == NULL)
    {
        return false;
    }
    else
    {
        return true;
    }
}

/* 비어 있는 첫 FD 번호를 반환한다. 빈 칸이 없으면 -1을 반환한다. */
static int
find_free_fd (struct thread *curr) {
    for (int fd = 2; fd < 50; fd++) {
        if (curr->file_discrpitor->file[fd] == NULL)
            return fd;
    }
    return -1;
}
struct lock filesys_lock;
/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
    switch (f->R.rax) {
        case SYS_HALT:
            power_off ();
            break;

        case SYS_EXIT:
            terminate_process ((int) f->R.rdi);
            break;

        case SYS_WRITE: {
            int file_discrpitor_index = (int) f->R.rdi;
            const void *buffer = (const void *) f->R.rsi;
            unsigned size = (unsigned) f->R.rdx;

            if (!valid_read_buffer (buffer, size)) {
                terminate_process (-1);
                break;
            }

            f->R.rax = -1;

            if (file_discrpitor_index == 1) {
                if (size > 0)
                {
                    putbuf (buffer, size);
                }
                f->R.rax = size;
                break;
            }

            if(file_discrpitor_index >= 50 || thread_current()->file_discrpitor == NULL) 
            {
                break;
            }
            lock_acquire (&filesys_lock);
            struct file * file = thread_current()->file_discrpitor->file[file_discrpitor_index];
            
            if(file != NULL)
            {
                f->R.rax = file_write (file, buffer, size);
            }
            
            lock_release (&filesys_lock);
            break;
        }

        case SYS_CREATE: {
            if(!vaild_addr(f->R.rdi))
            {
                terminate_process(-1);
                break;
            }
            char * file_name = (char *)f->R.rdi;
            const void *size = (const void *) f->R.rsi;
            unsigned temp = (unsigned) f->R.rdx;

            if(file_name == NULL || *file_name == NULL || *file_name == '\000')
            {
                terminate_process(-1);
                break;
            }
            lock_acquire (&filesys_lock);
            bool success = filesys_create (file_name, size);
            lock_release (&filesys_lock);
            if(success == false)
            {
                f->R.rax = 0;
                break;
            }

            f->R.rax = "1";
            break;
        }

        case SYS_OPEN: {
            if(!vaild_addr(f->R.rdi))
            {
                terminate_process(-1);
                break;
            }
            char * file_name = (char *)f->R.rdi;
            lock_acquire (&filesys_lock);
            struct file * file = filesys_open(file_name);
            struct thread * curr = thread_current();

            if (curr->file_discrpitor == NULL)
            {
                curr->file_discrpitor = malloc(sizeof *curr->file_discrpitor);
                if (curr->file_discrpitor == NULL)
                {
                    lock_release (&filesys_lock);
                    f->R.rax = -1;
                    break;
                }
                memset(curr->file_discrpitor, 0, sizeof *curr->file_discrpitor);
            }
            if (file == NULL)
            {
                lock_release (&filesys_lock);
                f->R.rax = -1;
                break;
            }

            int file_discrpitor_index = find_free_fd (curr);
            if (file_discrpitor_index == -1) {
                file_close (file);
                lock_release (&filesys_lock);
                f->R.rax = -1;
                break;
            }

            if (curr->file_discrpitor == NULL)
            {
                file_close (file);
                lock_release (&filesys_lock);
                f->R.rax = -1;
                break;
            }

            curr->file_discrpitor->file[file_discrpitor_index] = file;
            f->R.rax = file_discrpitor_index;
            lock_release (&filesys_lock);
            break;
        }
        case SYS_CLOSE: {
            int file_discrpitor_index = (int)f->R.rdi;
            if (file_discrpitor_index < 2 || file_discrpitor_index >= 50) 
            {
                terminate_process (-1);
                break;
            }

            lock_acquire (&filesys_lock);
            struct file * file = thread_current()->file_discrpitor->file[file_discrpitor_index];
            if(file != NULL)
            {
                file_close(file);
                thread_current()->file_discrpitor->file[file_discrpitor_index] = NULL;
            }
            lock_release (&filesys_lock);
            break;
        }

        case SYS_FORK: {
            char * process_name = (char *)f->R.rdi;
            tid_t tid = process_fork(process_name, f);
            f->R.rax = tid;
            break;
        }

        case SYS_WAIT: {
            int64_t pid = f->R.rdi;
            f->R.rax = process_wait (pid);
            break;
        }
        
        case SYS_EXEC: {
            if(!vaild_addr(f->R.rdi))
            {
                terminate_process(-1);
                break;
            }
            char * file_name = (char *)f->R.rdi;
            if(process_exec(file_name) == -1)
            {
                terminate_process(-1);
                break;
            }
            break;
        }

        case SYS_FILESIZE: {
            int file_discrpitor_index = (int) f->R.rdi;
            f->R.rax = -1;
            if (file_discrpitor_index < 2 || file_discrpitor_index >= 50) 
            {
                break;
            }
            if (thread_current()->file_discrpitor == NULL)
            {
                break;
            }

            lock_acquire (&filesys_lock);
            struct file * file = thread_current()->file_discrpitor->file[file_discrpitor_index];

            if (file != NULL) {
                off_t size = file_length (file);
                f->R.rax = size;
            }
            lock_release (&filesys_lock);
            break;
        }

        case SYS_READ: {
            int file_discrpitor_index = (int) f->R.rdi;
            void *buffer = (void *) f->R.rsi;
            unsigned size = (unsigned) f->R.rdx;

            if(!vaild_addr(f->R.rsi))
            {
                terminate_process(-1);
                break;
            }
            if (size == 0) {
                f->R.rax = 0;
                break;
            }
            if (file_discrpitor_index < 2 || file_discrpitor_index >= 50) {
                terminate_process (-1);
                break;
            }
            
            lock_acquire (&filesys_lock);
            struct file * file = thread_current()->file_discrpitor->file[file_discrpitor_index];

            if (file != NULL)
            {
                f->R.rax = file_read (file, buffer, size);
            }
            lock_release (&filesys_lock);
            break;
        }

        case SYS_SEEK: {
            int file_discrpitor_index = (int) f->R.rdi;
            int file_cursor = (int) f->R.rsi;
            if (file_discrpitor_index < 2 || file_discrpitor_index >= 50) {
                terminate_process (-1);
                break;
            }
            
            lock_acquire (&filesys_lock);
            struct file * file = thread_current()->file_discrpitor->file[file_discrpitor_index];
            file_seek(file, file_cursor);
            lock_release (&filesys_lock);

            break;
        }

        case SYS_REMOVE: {
            if(!vaild_addr(f->R.rdi))
            {
                terminate_process(-1);
                break;
            }

            char * file_name = (char *)f->R.rdi;
            
            if(file_name == NULL || *file_name == NULL || *file_name == '\000')
            {
                terminate_process(-1);
                break;
            }

            lock_acquire (&filesys_lock);
            bool success = filesys_remove(file_name);
            lock_release (&filesys_lock);
            if(success == false)
            {
                f->R.rax = 0;
                break;
            }

            f->R.rax = "1";
            break;
        }

        default:
            terminate_process (-1);
            break;
    }
}