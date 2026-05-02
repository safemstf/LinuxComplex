/* kernel/task.c - Task Management Implementation
 *
 * Implements process creation, destruction, and context switching.
 * UPDATED: Added process hierarchy management for fork/wait
 */

#include "task.h"
#include "kernel.h"
#include "elf.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "scheduler.h"

/* ================================================================
 * USER MODE MEMORY LAYOUT
 * ================================================================ */
#define USER_CODE_BASE 0x08048000 /* Standard Linux .text address */
/* USER_STACK_TOP is defined in vmm.h as 0xBFFFFFFF */
/* USER_STACK_SIZE is defined in vmm.h as 0x00100000 */
#define USER_HEAP_START 0x10000000 /* Heap starts at 256MB */

/* ================================================================
 * GLOBAL STATE
 * ================================================================ */
task_t *current_task = NULL;
static uint32_t next_pid = 1;
task_t *kernel_task = NULL;
static task_t *task_list_head = NULL;

/* ================================================================
 * FORWARD DECLARATIONS
 * ================================================================ */
static void task_setup_kernel_stack(task_t *task, void (*entry_point)(void));
extern void task_switch_asm(task_t *old_task, task_t *new_task);

/* ================================================================
 * INITIALIZATION
 * ================================================================ */
/* Idle loop - runs when no other tasks are ready */
static void kernel_idle_loop(void)
{
    while (1)
    {
        __asm__ volatile("hlt"); /* Halt until next interrupt */
    }
}

/* ================================================================
 * INITIALIZATION
 * ================================================================ */
void task_init(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("[TASK] Initializing task management...\n");

    kernel_task = kmalloc(sizeof(task_t));
    if (!kernel_task)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("[TASK] ERROR: Failed to allocate kernel task!\n");
        return;
    }

    memset(kernel_task, 0, sizeof(task_t));

    kernel_task->pid = 0;
    strcpy(kernel_task->name, "kernel_idle");
    kernel_task->state = TASK_RUNNING;
    kernel_task->priority = 255;
    kernel_task->ring = 0;        /* Kernel mode */
    kernel_task->time_slice = 10; /* Give kernel task initial time slice */
    kernel_task->page_directory = vmm_current_as->page_dir;
    kernel_task->parent = NULL;
    kernel_task->parent_pid = 0;
    kernel_task->first_child = NULL;
    kernel_task->next_sibling = NULL;
    kernel_task->next = NULL;
    kernel_task->waited = false;
    kernel_task->context_saved = false;

    /* Allocate kernel stack for idle task */
    uint32_t raw_kstack = (uint32_t)kmalloc(4096 + 4096);
    if (!raw_kstack)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("[TASK] ERROR: Failed to allocate kernel stack!\n");
        kfree(kernel_task);
        return;
    }

    kernel_task->kernel_stack_alloc = raw_kstack;
    kernel_task->kernel_stack = (raw_kstack + 0xFFF) & ~0xFFF;

    /* Setup stack with idle loop as entry point */
    task_setup_kernel_stack(kernel_task, kernel_idle_loop);

    current_task = kernel_task;
    task_list_head = kernel_task;

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[TASK] Kernel idle task created (PID 0)\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ================================================================
 * PROCESS HIERARCHY MANAGEMENT
 * ================================================================ */
void task_add_child(task_t *parent, task_t *child)
{
    if (!parent || !child)
        return;

    child->parent = parent;
    child->parent_pid = parent->pid;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

void task_remove_child(task_t *parent, task_t *child)
{
    if (!parent || !child)
        return;

    task_t *prev = NULL;
    task_t *curr = parent->first_child;

    while (curr)
    {
        if (curr == child)
        {
            if (prev)
            {
                prev->next_sibling = curr->next_sibling;
            }
            else
            {
                parent->first_child = curr->next_sibling;
            }
            child->parent = NULL;
            child->next_sibling = NULL;
            return;
        }
        prev = curr;
        curr = curr->next_sibling;
    }
}

/* ================================================================
 * KERNEL TASK CREATION
 * ================================================================ */
task_t *task_create(const char *name, void (*entry_point)(void), uint32_t priority)
{
    task_t *task = kmalloc(sizeof(task_t));
    if (!task)
    {
        terminal_writestring("[TASK] ERROR: Failed to allocate task structure\n");
        return NULL;
    }

    memset(task, 0, sizeof(task_t));

    /* Basic properties */
    task->pid = next_pid++;
    strncpy(task->name, name, 31);
    task->name[31] = '\0';
    task->state = TASK_READY;
    task->priority = priority;
    task->ring = 0; /* Kernel mode */
    task->time_slice = 10;
    task->total_time = 0;
    task->exit_code = 0;
    task->waited = false;

    /* Process hierarchy */
    task->parent = NULL;
    task->parent_pid = 0;
    task->first_child = NULL;
    task->next_sibling = NULL;

    /* Allocate kernel stack: produce a page-aligned stack and keep original pointer */
    uint32_t raw_kstack = (uint32_t)kmalloc(4096 + 4096);
    if (!raw_kstack)
    {
        terminal_writestring("[TASK] ERROR: Failed to allocate kernel stack\n");
        kfree(task);
        return NULL;
    }
    /* Align up to the next 4KB boundary */
    task->kernel_stack_alloc = raw_kstack; /* new field to store original pointer */
    task->kernel_stack = (raw_kstack + 0xFFF) & ~0xFFF;

    /* Allocate user stack */
    task->user_stack = (uint32_t)kmalloc(4096);
    if (!task->user_stack)
    {
        terminal_writestring("[TASK] ERROR: Failed to allocate user stack\n");
        if (task->kernel_stack_alloc)
            kfree((void *)task->kernel_stack_alloc);
        ;
        kfree(task);
        return NULL;
    }

    /* Use kernel address space */
    task->page_directory = kernel_task->page_directory;

    /* Setup initial stack */
    task_setup_kernel_stack(task, entry_point);

    /* Set parent to current task */
    if (current_task)
    {
        task_add_child(current_task, task);
    }

    /* Add to task list */
    task->next = task_list_head;
    task_list_head = task;

    return task;
}

/* ================================================================
 * USER TASK CREATION (Phase 4) - FIXED VERSION
 * ================================================================ */
task_t *task_create_user(const char *name, void *elf_data, uint32_t priority)
{
    task_t *task = kmalloc(sizeof(task_t));
    if (!task)
        return NULL;

    memset(task, 0, sizeof(task_t));

    /* Basic properties */
    task->pid = next_pid++;
    strncpy(task->name, name, 31);
    task->name[31] = '\0';
    task->state = TASK_READY;
    task->priority = priority;
    task->ring = 3; /* USER MODE */
    task->time_slice = 10;
    task->first_run = true;

    /* Create a new address space */
    struct vmm_address_space *as = vmm_create_as();
    if (!as)
    {
        kfree(task);
        return NULL;
    }

    task->address_space = as;
    task->page_directory = as->page_dir;

    /* Allocate kernel stack (page-aligned) */
    uint32_t raw_kstack = (uint32_t)kmalloc(4096 + 4096);
    if (!raw_kstack)
    {
        vmm_destroy_as(as);
        kfree(task);
        return NULL;
    }

    task->kernel_stack_alloc = raw_kstack;
    task->kernel_stack = (raw_kstack + 0xFFF) & ~0xFFF;

    /* Allocate user stack (physical page) */
    uint32_t ustack_phys = (uint32_t)pmm_alloc_block();
    if (!ustack_phys)
    {
        kfree((void *)raw_kstack);
        vmm_destroy_as(as);
        kfree(task);
        return NULL;
    }

    task->user_stack_phys = ustack_phys;

    /* Map user stack into task's address space */
    vmm_map_page_in_as(
        task->address_space,
        USER_STACK_TOP - PAGE_SIZE + 1,
        ustack_phys,
        VMM_PRESENT | VMM_WRITE | VMM_USER);

    task->user_esp = USER_STACK_TOP - 4;
    task->stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    /* Load ELF */
    if (!elf_load(task, elf_data))
    {
        vmm_unmap_page_in_as(task->address_space, USER_STACK_TOP - PAGE_SIZE);
        pmm_free_block((void *)ustack_phys);
        kfree((void *)raw_kstack);
        vmm_destroy_as(as);
        kfree(task);
        return NULL;
    }

    /* Build IRET frame */
    task_setup_user_context(task);

    /* Set parent */
    if (current_task)
        task_add_child(current_task, task);

    task->next = task_list_head;
    task_list_head = task;

    return task;
}

/* ================================================================
 * STACK SETUP - KERNEL MODE
 * ================================================================ */
static void task_setup_kernel_stack(task_t *task, void (*entry_point)(void))
{
    uint32_t *stack = (uint32_t *)(task->kernel_stack + 4096);

    /* Push initial values (will be popped by task_switch_asm) */
    *(--stack) = 0x202;                 /* EFLAGS (interrupts enabled) */
    *(--stack) = 0x08;                  /* CS (kernel code segment) */
    *(--stack) = (uint32_t)entry_point; /* EIP */
    *(--stack) = 0;                     /* EAX */
    *(--stack) = 0;                     /* ECX */
    *(--stack) = 0;                     /* EDX */
    *(--stack) = 0;                     /* EBX */
    *(--stack) = 0;                     /* ESP (ignored) */
    *(--stack) = 0;                     /* EBP */
    *(--stack) = 0;                     /* ESI */
    *(--stack) = 0;                     /* EDI */

    task->context.esp = (uint32_t)stack;
    task->context.eip = (uint32_t)entry_point;
}

/* ================================================================
 * STACK SETUP - USER MODE
 * ================================================================ */
void task_setup_user_context(task_t *task)
{
    uint32_t *kstack = (uint32_t *)(task->kernel_stack + 4096);

    /*
     * iret frame for ring 3 transition
     * CPU pops in this order:
     *   EIP, CS, EFLAGS, ESP, SS
     */

    *(--kstack) = 0x23;              /* SS (user data) */
    *(--kstack) = task->user_esp;    /* ESP */
    *(--kstack) = 0x202;             /* EFLAGS (IF=1) */
    *(--kstack) = 0x1B;              /* CS (user code) */
    *(--kstack) = task->entry_point; /* EIP */

    task->context.esp = (uint32_t)kstack;
    task->context.eip = task->entry_point;
}

/* Assembly helper to save kernel context and IRET to user mode */
extern void task_switch_to_user_asm(task_t *old_task, task_t *new_task);

/* ================================================================
 * TASK SWITCHING
 * ================================================================ */
void task_switch(task_t *new_task)
{
    __asm__ volatile("cli");

    if (!new_task || new_task == current_task)
    {
        __asm__ volatile("sti");
        return;
    }

    task_t *old_task = current_task;

    /* If old task is exiting (ZOMBIE), don't save its context */
    if (old_task && old_task->state == TASK_ZOMBIE)
    {
        /* Update states */
        new_task->state = TASK_RUNNING;
        current_task = new_task;

        /* Switch page directory if different */
        if (new_task->page_directory &&
            (!old_task || new_task->page_directory != old_task->page_directory))
        {
            uint32_t phys = (uint32_t)new_task->page_directory;
            __asm__ volatile("mov %0, %%cr3" ::"r"(phys));
        }

        /* Update TSS */
        extern void tss_set_kernel_stack(uint32_t stack);
        tss_set_kernel_stack(new_task->kernel_stack + 4096);

        /* Don't save old task's context - it's dead anyway */
        /* Load new task's context directly */
        task_switch_and_die(new_task);
        /* Never returns */
    }

    /* Normal context switch for non-exiting tasks */
    if (old_task && old_task->state == TASK_RUNNING)
        old_task->state = TASK_READY;

    new_task->state = TASK_RUNNING;
    current_task = new_task;

    /* Switch page directory if different */
    if (new_task->page_directory &&
        (!old_task || new_task->page_directory != old_task->page_directory))
    {
        uint32_t phys = (uint32_t)new_task->page_directory;
        __asm__ volatile("mov %0, %%cr3" ::"r"(phys));
    }

    /* Update TSS */
    extern void tss_set_kernel_stack(uint32_t stack);
    tss_set_kernel_stack(new_task->kernel_stack + 4096);

    if (old_task == kernel_task && !kernel_task->context_saved)
    {
        kernel_task->context_saved = true;
        /* Let task_switch_asm save kernel context */
    }

    /* USER MODE: Use assembly to save old context AND IRET to user mode */
    if (new_task->ring == 3 && new_task->first_run)
    {
        new_task->first_run = false;
        /* Use assembly function that saves old_task context then IRETs */
        task_switch_to_user_asm(old_task, new_task);
        /* When we return here, user task has finished and we're back */
        return;
    }

    /* Kernel mode - use assembly context switch */
    task_switch_asm(old_task, new_task);
}

/* ================================================================
 * TASK QUERIES
 * ================================================================ */
task_t *task_current(void)
{
    return current_task;
}

task_t *task_find_by_pid(uint32_t pid)
{
    task_t *task = task_list_head;
    while (task)
    {
        if (task->pid == pid)
            return task;
        task = task->next;
    }
    return NULL;
}

/* ================================================================
 * TASK LIFECYCLE
 * ================================================================ */
void task_exit(int exit_code)
{
    if (!current_task || current_task == kernel_task)
    {
        return;
    }

    /* Mark as zombie FIRST */
    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = exit_code;

    /* Wake up parent if it's waiting */
    if (current_task->parent && current_task->parent->state == TASK_BLOCKED)
    {
        task_unblock(current_task->parent);
    }

    /* Switch to scheduler without saving context */
    extern void task_switch_and_die(task_t *);
    extern task_t *scheduler_pick_next(void);
    
    task_t *next = scheduler_pick_next();
    if (!next) next = kernel_task;
    
    /* Don't use normal task_switch - it will try to save dead context */
    task_switch_and_die(next);
    
    /* Never returns */
    __builtin_unreachable();
}

void task_yield(void)
{
    extern void scheduler_schedule(void);
    scheduler_schedule();
}

void task_block(void)
{
    if (current_task)
    {
        current_task->state = TASK_BLOCKED;
        task_yield();
    }
}

void task_unblock(task_t *task)
{
    if (task && task->state == TASK_BLOCKED)
    {
        task->state = TASK_READY;
    }
}

void task_sleep(uint32_t ms)
{
    if (!current_task)
        return;

    extern scheduler_stats_t scheduler_get_stats(void);
    scheduler_stats_t stats = scheduler_get_stats();

    current_task->wake_time = stats.total_ticks + ms;
    current_task->state = TASK_SLEEPING;

    task_yield();
}

void task_destroy(task_t *task)
{
    if (!task || task == kernel_task)
    {
        return;
    }

    /* Remove from scheduler's ready queue */
    extern void scheduler_remove_task(task_t * task);
    scheduler_remove_task(task);

    /* Remove from parent's child list */
    if (task->parent)
    {
        task_remove_child(task->parent, task);
    }

    /* Remove from task list */
    if (task_list_head == task)
    {
        task_list_head = task->next;
    }
    else
    {
        task_t *prev = task_list_head;
        while (prev && prev->next != task)
        {
            prev = prev->next;
        }
        if (prev)
        {
            prev->next = task->next;
        }
    }

    /* Free kernel stack - both kernel and user tasks use kmalloc for kernel stacks */
    if (task->kernel_stack_alloc)
    {
        kfree((void *)task->kernel_stack_alloc);
    }

    /* Free user stack and address space for user tasks */
    if (task->ring == 3)
    {
        /* Unmap and free user stack */
        if (task->user_stack_phys)
        {
            vmm_unmap_page(USER_STACK_TOP - PAGE_SIZE);
            pmm_free_block((void *)task->user_stack_phys);
        }

        /* Destroy address space */
        if (task->address_space)
        {
            vmm_destroy_as(task->address_space);
        }
    }
    else
    {
        /* Kernel task: free user stack if allocated */
        if (task->user_stack)
        {
            kfree((void *)task->user_stack);
        }
    }

    /* Free task structure */
    kfree(task);
}