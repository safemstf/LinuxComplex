/* kernel/task.h - Process/Task Management
 *
 * Defines the fundamental structures for multitasking:
 * - Task control block (TCB)
 * - Task states and priorities
 * - CPU register context
 *
 * UPDATED: Added parent-child process hierarchy for fork/wait
 */

#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * TASK STATES
 * ================================================================ */
typedef enum
{
    TASK_READY,    /* Ready to run, waiting for CPU */
    TASK_RUNNING,  /* Currently executing on CPU */
    TASK_BLOCKED,  /* Waiting for I/O or event */
    TASK_SLEEPING, /* Sleeping for timer */
    TASK_ZOMBIE    /* Finished, waiting to be cleaned up */
} task_state_t;

/* ================================================================
 * CPU CONTEXT
 * All CPU registers that need to be saved/restored on context switch
 * ================================================================ */
typedef struct
{
    /* General purpose registers (saved by pusha) */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp; /* Stack pointer */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    /* Segment registers */
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;

    /* Instruction pointer and flags */
    uint32_t eip; /* Where to resume execution */
    uint32_t cs;
    uint32_t eflags;

    /* User stack (for returning from kernel mode) */
    uint32_t user_esp;
    uint32_t ss;
} cpu_context_t;

/* ================================================================
 * TASK CONTROL BLOCK (TCB)
 * Everything the OS needs to know about a process
 * ================================================================ */
typedef struct task
{
    /* Task identification */
    uint32_t pid;  /* Process ID */
    char name[32]; /* Task name (for debugging) */

    /* Task state */
    task_state_t state; /* Current state */
    uint32_t priority;  /* Priority (0 = highest) */

    /* CPU context - saved/restored on context switch */
    cpu_context_t context;

    /* Memory management */
    uint32_t *page_directory; /* Virtual address space */
    uint32_t kernel_stack;    /* Kernel mode stack */
    uint32_t user_stack;      /* User mode stack */
    /* in task_t struct (task.h) */
    uint32_t kernel_stack_alloc; /* original pointer returned by kmalloc (for freeing) */

    /* USER MODE SUPPORT (Phase 4) */
    uint8_t ring;      /* 0 = kernel task, 3 = user task */
    uint32_t user_esp; /* Ring 3 stack pointer */

    /* User memory layout */
    uint32_t code_start;                     /* Code segment start */
    uint32_t code_end;                       /* Code segment end */
    uint32_t entry_point;                    /* Entry point address */
    uint32_t data_start;                     /* Data segment start */
    uint32_t data_end;                       /* Data segment end */
    uint32_t heap_start;                     /* Heap start */
    uint32_t heap_end;                       /* Heap end */
    uint32_t stack_bottom;                   /* User stack bottom */
    struct vmm_address_space *address_space; /* Address space for user tasks */
    uint32_t user_stack_phys;                /* Physical address of user stack */

    /* Scheduling */
    uint32_t time_slice; /* Remaining time quantum (ticks) */
    uint32_t total_time; /* Total CPU time used */
    uint32_t wake_time;  /* Wake up at this tick (for TASK_SLEEPING) */

    /* PROCESS HIERARCHY (Phase 5) */
    struct task *parent;       /* Parent task */
    uint32_t parent_pid;       /* Parent PID (for safety) */
    struct task *first_child;  /* First child in linked list */
    struct task *next_sibling; /* Next sibling */

    /* Task list (all tasks) */
    struct task *next; /* Next in global task list */

    /* Scheduler ready queue */
    struct task *sched_next; /* Next in scheduler ready queue */
    bool first_run;
    bool context_saved;
    
    /* Exit status */
    int exit_code; /* Return value when task exits */
    bool waited;   /* Has parent waited for this zombie? */
} task_t;

/* ================================================================
 * TASK MANAGEMENT FUNCTIONS
 * ================================================================ */

/* Initialize task subsystem */
void task_init(void);

/* Create a new kernel task */
task_t *task_create(const char *name, void (*entry_point)(void), uint32_t priority);

/* Create a new user mode task from ELF binary */
task_t *task_create_user(const char *name, void *elf_data, uint32_t priority);

/* Setup user mode context for a task */
void task_setup_user_context(task_t *task);

/* Destroy a task */ //todo: zombie_queue_add(child); repear que implementation would make life easier
void task_destroy(task_t *task);

/* Get current running task */
task_t *task_current(void);

/* Switch to another task */
void task_switch(task_t *new_task);

/* Exit current task */
void task_exit(int exit_code);

/* Yield CPU to another task */
void task_yield(void);

/* Block current task (wait for event) */
void task_block(void);

/* Unblock a task (event occurred) */
void task_unblock(task_t *task);

/* Sleep for N milliseconds */
void task_sleep(uint32_t ms);

/* ================================================================
 * PROCESS HIERARCHY FUNCTIONS (Phase 5)
 * ================================================================ */

/* Add child to parent's child list */
void task_add_child(task_t *parent, task_t *child);

/* Remove child from parent's child list */
void task_remove_child(task_t *parent, task_t *child);

/* ================================================================
 * TASK QUERIES
 * ================================================================ */

/* Find a task by PID */
task_t *task_find_by_pid(uint32_t pid);

/* ================================================================
 * KERNEL TASK
 * ================================================================ */
extern task_t *kernel_task;
extern task_t *current_task;

#endif /* TASK_H */