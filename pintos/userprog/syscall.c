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
            int fd = (int) f->R.rdi;
            const void *buffer = (const void *) f->R.rsi;
            unsigned size = (unsigned) f->R.rdx;

            if (!valid_read_buffer (buffer, size)) {
                terminate_process (-1);
                break;
            }

            if (fd != 1) {
                f->R.rax = -1;
                break;
            }

            if (size > 0)
                putbuf (buffer, size);

            f->R.rax = size;
            break;
        }

        case SYS_CREATE: {
            if(!vaild_addr(f->R.rdi))
            {
                terminate_process(-1);
                break;
            }
            // f->R.rdi	"hello.txt" 문자열의 주소
            // f->R.rsi	초기 크기 100
            // f->R.rax	시스템 콜 번호 SYS_CREATE rax가 0이면 false
            //int addr = (int) f->R.rdi;
            char * file_name = (char *)f->R.rdi;
            const void *size = (const void *) f->R.rsi;
            unsigned temp = (unsigned) f->R.rdx;

            if(file_name == NULL || *file_name == NULL || *file_name == '\000')
            {
                terminate_process(-1);
                break;
            }

            bool success = filesys_create (file_name, size);
            if(success == false)
            {
                f->R.rax = 0;
                break;
            }

            f->R.rax = "1";
            break;
        }

        case SYS_OPEN: {
            char * file_name = (char *)f->R.rdi;
            struct file * file = filesys_open(file_name);
            struct thread * curr = thread_current();
            int64_t file_discrpitor_index = 2;

            if (curr->file_discrpitor == NULL)
            {
                curr->file_discrpitor = malloc(sizeof *curr->file_discrpitor);
                if (curr->file_discrpitor == NULL)
                {
                    f->R.rax = -1;
                    break;
                }
                memset(curr->file_discrpitor, 0, sizeof *curr->file_discrpitor);
            }
            if (file == NULL)
            {
                f->R.rax = -1;
                break;
            }
            curr->file_discrpitor->file[file_discrpitor_index] = file;
            f->R.rax = file_discrpitor_index;
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
        

        default:
            terminate_process (-1);
            break;
    }
}