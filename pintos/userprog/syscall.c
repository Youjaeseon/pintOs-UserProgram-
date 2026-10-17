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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
    printf ("%s: exit(%d)\n", thread_name (), status);
    thread_exit ();
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

        default:
            terminate_process (-1);
            break;
    }
}
