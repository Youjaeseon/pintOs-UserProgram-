#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* initd와 다른 프로세스에 공통으로 적용하는 초기화 함수. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* FILE_NAME에서 불러온 첫 사용자 프로그램인 "initd"를 시작한다.
 * process_create_initd()가 반환하기 전에 새 스레드가 스케줄링되거나
 * 실행을 마치고 종료될 수도 있다. initd의 스레드 ID를 반환하며,
 * 스레드를 생성할 수 없으면 TID_ERROR를 반환한다. 
 * 이 함수는 반드시 한 번만 호출해야 한다. */
/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t //실행할 명령 문자열을 복사하고 스레드 생성.
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* FILE_NAME의 복사본을 만든다.
	 * 복사하지 않으면 호출자와 load() 사이에 경쟁 상태가 발생할 수 있다. */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);


	size_t buffer_size = strlen(file_name) + 1;
	char * fn_token = malloc(buffer_size);
    char * token = NULL;
	char * save_ptr = NULL;

	if (fn_token != NULL) {
    	strlcpy(fn_token, file_name, buffer_size);
   		token = strtok_r(fn_token, " ", &save_ptr);
	}

	ASSERT(token);


	/* FILE_NAME을 실행할 새 스레드를 생성한다. */
	tid = thread_create (token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
	{
		palloc_free_page (fn_copy);
    	free(fn_token);
	}
	return tid;
}

/* 첫 사용자 프로세스를 실행하는 스레드 함수. */
static void
initd (void *f_name) { //프로세스 초기화
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* 현재 프로세스를 복제하여 이름이 `name`인 새 프로세스를 만든다.
 * 새 프로세스의 스레드 ID를 반환하며, 스레드를 생성할 수 없으면
 * TID_ERROR를 반환한다. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* 현재 스레드를 새 스레드로 복제한다. */
	return thread_create (name,
			PRI_DEFAULT, __do_fork, thread_current ());
}

#ifndef VM
/* 이 함수를 pml4_for_each에 전달하여 부모의 주소 공간을 복제한다.
 * 이 함수는 Project 2에서만 사용한다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */

	/* 2. 부모의 최상위 페이지 테이블(PML4)에서 VA에 대응하는 페이지를 찾는다. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */

	/* 5. 자식의 페이지 테이블에서 주소 VA에 새 페이지를 연결하고,
	 *    WRITABLE에 따라 쓰기 권한을 설정한다. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
	}
	return true;
}
#endif

/* 부모의 실행 문맥을 복사하는 스레드 함수.
 * 힌트) parent->tf에는 프로세스의 사용자 모드 실행 문맥이 들어 있지 않다.
 *       따라서 process_fork의 두 번째 인자를 이 함수에 전달해야 한다. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. CPU 실행 문맥을 지역 변수 if_가 있는 스택으로 복사한다. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. 페이지 테이블(PT)을 복제한다. */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	process_init ();

	/* 마지막으로 새로 생성한 프로세스로 실행을 전환한다. */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}

/* 현재 실행 문맥을 f_name에 해당하는 프로그램으로 교체한다.
 * 실패하면 -1을 반환한다. */
int
process_exec (void *f_name) { //유저 프로그램 실행에 사용할 레지스터 상태 준비.
	char *file_name = f_name;
	bool success;

	/* thread 구조체 안의 intr_frame은 사용할 수 없다.
	 * 현재 스레드가 다시 스케줄링될 때 해당 멤버에
	 * 스레드의 실행 정보가 저장되기 때문이다. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* 먼저 현재 실행 문맥을 정리한다. */
	process_cleanup ();

	/* 그런 다음 실행 파일을 메모리에 적재한다. */
	success = load (file_name, &_if);

	/* 적재에 실패하면 함수를 종료한다. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* 교체된 프로그램의 실행을 시작한다. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* 스레드 TID가 종료될 때까지 기다린 뒤 종료 상태를 반환한다.
 * 커널에 의해 종료된 경우, 즉 예외 때문에 종료된 경우에는 -1을 반환한다.
 * TID가 유효하지 않거나 호출한 프로세스의 자식이 아닌 경우,
 * 또는 해당 TID에 대한 process_wait() 호출이 이미 성공한 경우에는
 * 기다리지 않고 즉시 -1을 반환한다.
 *
 * 이 함수는 문제 2-2에서 구현한다. 현재는 아무 작업도 하지 않는다. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: 힌트) process_wait(initd)가 반환하면 Pintos가 종료되므로,
	 * XXX:       process_wait를 구현하기 전에는 이곳에 무한 루프를
	 * XXX:       추가하는 것을 권장한다. */
	struct thread * child_thread = find_from_all_thread_list(child_tid);
	if(child_thread == NULL)
	{
		return -1;
	}
	struct thread * curr = thread_current();
	child_thread->kernel_thread_semaphore = &curr->wait_semaphore;
	sema_down(&curr->wait_semaphore);
	return 0;
}

/* 프로세스를 종료한다. thread_exit()에서 호출하는 함수이다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	if (curr->pml4 != NULL) {
    printf("%s: exit(%d)\n", curr->name, curr->exit_status);
    }

	 
	process_cleanup ();
	sema_up(curr->kernel_thread_semaphore);
	//printf ("%s: exit(%d)\n", ...);
	//printf ("args-single: exit(0)\n");
	
}

/* 현재 프로세스의 자원을 해제한다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 테이블을 제거하고,
	 * 커널 주소만 매핑된 페이지 테이블로 전환한다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서는 처리 순서가 매우 중요하다. 페이지 테이블을 전환하기 전에
		 * cur->pagedir(현재 코드의 curr->pml4)을 NULL로 설정해야 한다.
		 * 그래야 타이머 인터럽트가 발생해도 이 프로세스의 페이지 테이블로
		 * 다시 전환되지 않는다. 또한 프로세스의 페이지 테이블을 제거하기 전에
		 * 기본 페이지 테이블을 활성화해야 한다. 그렇지 않으면 이미 해제되어
		 * 내용까지 지워진 페이지 테이블을 CPU가 계속 사용하게 된다. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 다음 스레드에서 사용자 코드를 실행하도록 CPU를 설정한다.
 * 문맥 전환이 일어날 때마다 호출한다. */
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화한다. */
	pml4_activate (next->pml4);

	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정한다. */
	tss_update (next);
}

/* ELF 형식의 실행 파일을 적재한다. 아래 정의들은
 * ELF 명세 [ELF1]의 내용을 거의 그대로 가져온 것이다. */

/* ELF 관련 형식. [ELF1]의 1-2절을 참고한다. */
#define EI_NIDENT 16

#define PT_NULL    0            /* 무시한다. */
#define PT_LOAD    1            /* 적재 가능한 세그먼트. */
#define PT_DYNAMIC 2            /* 동적 링크 정보. */
#define PT_INTERP  3            /* 동적 로더의 이름. */
#define PT_NOTE    4            /* 부가 정보. */
#define PT_SHLIB   5            /* 예약된 값. */
#define PT_PHDR    6            /* 프로그램 헤더 테이블. */
#define PT_STACK   0x6474e551   /* 스택 세그먼트. */

#define PF_X 1          /* 실행 가능. */
#define PF_W 2          /* 쓰기 가능. */
#define PF_R 4          /* 읽기 가능. */

/* 실행 파일 헤더. [ELF1]의 1-4절부터 1-8절까지를 참고한다.
 * ELF 실행 파일의 맨 앞에 위치한다. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* 축약 이름. */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* FILE_NAME의 ELF 실행 파일을 현재 스레드에 적재한다.
 * *RIP에는 실행 시작 주소를, *RSP에는 초기 스택 포인터를 저장한다.
 * 성공하면 true, 실패하면 false를 반환한다. */
/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
// 인자를 전달하는 부분이 없다.
static bool
load (const char *file_name, struct intr_frame *if_) { //실행 파일을 메모리에 올리고, 유저 프로그램이 실행을 시작할 수 있는 상태를 만들기 위한 함수.
	struct thread *t = thread_current ();
	struct ELF ehdr; //실행 파일의 ELF 헤더를 읽어 둘 공간.
	struct file *file = NULL; // 열린 실행 파일을 커널에서 다루는 객체.
	off_t file_ofs; //파일의 어느 위치에서 읽을지 나타내는 값.
	bool success = false;
	int i; // 프로그램 헤더를 순회하는 인덱스.

	/* Allocate and activate page directory. */
	// 페이지 할당 및 활성화. 즉, 가상주소를 실제 메모리로 연결하는 페이지 테이블.
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ()); // 현재 프로세스의 주소 공간을 활성화하는 단계.

	/* 실행 파일을 연다. */

	size_t buffer_size = strlen(file_name) + 1;
	char *buffer = malloc(buffer_size+1);
	strlcpy(buffer, file_name, buffer_size);
	char * name_save_ptr = NULL;
	char * program_name = strtok_r(buffer, " ", &name_save_ptr);

	file = filesys_open (program_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* 실행 파일 헤더를 읽고 유효한지 검사한다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더들을 읽는다. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트는 무시한다. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD: // 메모리에 올릴 영역
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* 일반 세그먼트.
						 * 앞부분은 디스크에서 읽고 나머지는 0으로 채운다. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* 전체를 0으로 채운다.
						 * 디스크에서는 아무것도 읽지 않는다. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 스택을 설정한다. */
	if (!setup_stack (if_))
		goto done;


	/* 실행 시작 주소를 설정한다. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). parsing passing */


	char *token_save_ptr = NULL;
	size_t token_buffer_size = strlen(file_name) + 1;
	char *token_buffer = malloc(token_buffer_size+1);
	strlcpy (token_buffer, file_name, buffer_size);
	char *token;
	int rdi = 0;
	char **my_argv[50];
	char *token_temp[50];
	size_t size = 0;
	size_t all_size = 0;

	//역순해야함 args-single onearg
	for (token = strtok_r (token_buffer, " ", &token_save_ptr); token != NULL;
   	token = strtok_r (NULL, " ", &token_save_ptr))
   	{
		size = strlen(token) + 1;
		all_size += size;
		if_->rsp -= size;
		memcpy((void *) if_->rsp, token, size);
		
		token_temp[rdi] = token;
		my_argv[rdi] = if_->rsp;
		rdi++;
   	}

	// int index2 = rdi-1;
	// while (index2 > -1)
	// {
	// 	size_t size = strlen(token_temp[index2]) + 1;
	// 	all_size += size;
	// 	if_->rsp -= size;
	// 	memcpy((void *) if_->rsp, token_temp[index2], size);
	// 	index2--;
	// }

	if(all_size % 8 != 0)
	{
		size_t padding = ((all_size % 8) - 8) * (-1);
		if_->rsp -= padding;
		memset((void *) if_->rsp, 0, padding);
	}
	if_->rsp -= 8;
	memset((void *) if_->rsp, 0, 8);

	int index = rdi-1;
	while (index > -1)
	{
		if_->rsp -= 8;
		memcpy((void *) if_->rsp, &my_argv[index], 8);
		index--;
	}
	
	if_->R.rsi = if_->rsp;
	
	if_->rsp -= 8;
	memset((void *) if_->rsp, 0, 8);

	if_->R.rdi = (uint64_t)rdi;
	
	free(buffer);
	free(token_buffer);
	//free(token_temp);

	success = true;

done:
	/* 적재의 성공 여부와 관계없이 이곳에서 마무리한다. */
	file_close (file);
	return success;
}


/* PHDR가 FILE 안의 유효하고 적재 가능한 세그먼트를 나타내는지 검사한다.
 * 유효하면 true, 그렇지 않으면 false를 반환한다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr의 페이지 내부 오프셋은 같아야 한다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE의 범위 안을 가리켜야 한다. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 p_filesz보다 크거나 같아야 한다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 비어 있으면 안 된다. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역의 시작 주소와 끝 주소가 모두
	   사용자 주소 공간의 범위 안에 있어야 한다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 주소 계산이 넘쳐서 영역의 끝이 시작보다 작은 주소로
	   되돌아가는 현상을 허용하지 않는다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 0번 페이지의 매핑을 금지한다.
	   0번 페이지를 매핑하는 것 자체도 바람직하지 않지만, 이를 허용하면
	   사용자 코드가 시스템 콜에 널 포인터를 전달했을 때 memcpy() 등의
	   널 포인터 검증에 걸려 커널 패닉이 발생할 수 있다. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* 모든 검사를 통과했다. */
	return true;
}

#ifndef VM
/* 이 블록의 코드는 Project 2에서만 사용한다.
 * Project 2 전체에서 사용할 함수를 구현하려면
 * #ifndef 조건부 블록 밖에 구현한다. */

/* load()의 보조 함수들. */
static bool install_page (void *upage, void *kpage, bool writable);

/* FILE의 오프셋 OFS에서 시작하는 세그먼트를 주소 UPAGE에 적재한다.
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를
 * 다음과 같이 초기화한다.
 *
 * - UPAGE에서 시작하는 READ_BYTES 바이트는
 *   FILE의 오프셋 OFS부터 읽어 채운다.
 *
 * - UPAGE + READ_BYTES에서 시작하는 ZERO_BYTES 바이트는 0으로 채운다.
 *
 * WRITABLE이 true이면 사용자 프로세스가 초기화된 페이지에 쓸 수 있어야 하며,
 * false이면 읽기만 가능해야 한다.
 *
 * 성공하면 true를 반환한다. 메모리 할당 오류나
 * 디스크 읽기 오류가 발생하면 false를 반환한다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고,
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채운다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 메모리 한 페이지를 할당받는다. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* 이 페이지에 파일 내용을 적재한다. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* 프로세스의 주소 공간에 이 페이지를 매핑한다. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* 다음 페이지를 처리하도록 남은 크기와 주소를 갱신한다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK 바로 아래에 0으로 채운 한 페이지를 매핑하여 최소한의 스택을 만든다. */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* 사용자 가상 주소 UPAGE와 커널 가상 주소 KPAGE의 연결을
 * 페이지 테이블에 추가한다.
 * WRITABLE이 true이면 사용자 프로세스가 이 페이지를 수정할 수 있고,
 * false이면 읽기만 가능하다.
 * UPAGE에는 아직 다른 페이지가 매핑되어 있으면 안 된다.
 * KPAGE는 일반적으로 palloc_get_page()를 사용해
 * 사용자 풀에서 할당받은 페이지여야 한다.
 * 성공하면 true를 반환한다. UPAGE에 이미 매핑이 있거나
 * 메모리 할당에 실패하면 false를 반환한다. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* 해당 가상 주소에 이미 매핑된 페이지가 없는지 확인한 뒤,
	 * 이 페이지를 그 주소에 매핑한다. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
