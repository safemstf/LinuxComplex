/* kernel/syscall.c - System Call Handler
 *
 * UPDATED: Full implementations of fork, wait, exec
 */

#include "syscall.h"
#include "kernel.h"
#include "scheduler.h"
#include "task.h"
#include "elf.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"

/* ================================================================
 * HELPER FUNCTIONS
 * ================================================================ */

/* Clone a page directory for fork() */
static uint32_t *clone_page_directory(uint32_t *src_pd)
{
    /* Allocate new page directory - use PMM for page-aligned allocation */
    uint32_t *new_pd = (uint32_t *)pmm_alloc_block();
    if (!new_pd)
    {
        terminal_writestring("[FORK] Failed to allocate page directory\n");
        return NULL;
    }

    /* Clear it */
    memset(new_pd, 0, 4096);

    /* Copy kernel mappings (upper 1GB) - these are shared */
    for (int i = 768; i < 1024; i++)
    {
        new_pd[i] = src_pd[i];
    }

    /* Clone user space mappings (lower 3GB) */
    for (int i = 0; i < 768; i++)
    {
        if (!(src_pd[i] & 0x1))
            continue; /* Not present, skip */

        /* Get source page table */
        uint32_t *src_pt = (uint32_t *)(src_pd[i] & ~0xFFF);

        /* Allocate new page table - use PMM for page-aligned allocation */
        uint32_t *new_pt = (uint32_t *)pmm_alloc_block();
        if (!new_pt)
        {
            terminal_writestring("[FORK] Failed to allocate page table\n");
            /* TODO: proper cleanup on failure */
            return NULL;
        }

        /* Copy page table entries */
        for (int j = 0; j < 1024; j++)
        {
            if (!(src_pt[j] & 0x1))
                continue; /* Not present, skip */

            /* Allocate new physical page */
            uint32_t new_phys = (uint32_t)pmm_alloc_block();
            if (!new_phys)
            {
                terminal_writestring("[FORK] Failed to allocate physical page\n");
                /* TODO: proper cleanup */
                return NULL;
            }

            /* Get source physical page */
            uint32_t src_phys = src_pt[j] & ~0xFFF;

            /* Copy page contents - need to map both temporarily */
            /* For simplicity, assuming we can access them directly */
            memcpy((void *)new_phys, (void *)src_phys, 4096);

            /* Set page table entry with same flags */
            new_pt[j] = new_phys | (src_pt[j] & 0xFFF);
        }

        /* Set page directory entry with same flags */
        new_pd[i] = ((uint32_t)new_pt) | (src_pd[i] & 0xFFF);
    }

    return new_pd;
}

/* Note: Child's context is copied from parent's task structure in sys_fork(),
 * not from the interrupt registers. The child will resume with the same
 * context as the parent had when fork() was called. */

/* ================================================================
 * SYSCALL IMPLEMENTATIONS
 * ================================================================ */

/* Assembly function to switch to a task and NEVER return (for dying tasks) */
extern void task_switch_and_die(task_t *new_task);

void sys_exit(int code)
{
    terminal_writestring("\n[sys_exit] ENTERED\n");

    if (!current_task || current_task == kernel_task)
    {
        terminal_writestring("[sys_exit] ERROR: no task or kernel task\n");
        return;
    }

    task_t *dying = current_task;

    terminal_writestring("[sys_exit] Dying task: ");
    terminal_writestring(dying->name);
    terminal_writestring("\n");

    /* Mark zombie and store exit code */
    dying->exit_code = code;
    dying->state = TASK_ZOMBIE;

    /* Remove from run queues */
    scheduler_remove_task(dying);

    /* Wake up parent if waiting */
    if (dying->parent && dying->parent->state == TASK_BLOCKED)
    {
        task_unblock(dying->parent);
    }

    /*
     * CRITICAL:
     * We are still running on the dying task's kernel stack.
     * The only safe thing to do is yield to the scheduler.
     */
    scheduler_schedule();

    /* We must never return here */
    __builtin_unreachable();
}

int sys_write(const char *msg)
{
    /* Validate pointer is in user space (< 0xC0000000) */
    if ((uint32_t)msg >= 0xC0000000)
    {
        return -1;
    }

    terminal_writestring(msg);
    return 0;
}

int sys_read(char *buf, size_t len)
{
    /* TODO: Implement keyboard read */
    (void)buf;
    (void)len;
    return -1;
}

void sys_yield(void)
{
    task_yield();
}

uint32_t sys_getpid(void)
{
    return current_task ? current_task->pid : 0;
}

void sys_sleep(uint32_t ms)
{
    task_sleep(ms);
}

int sys_fork(void)
{
    if (!current_task)
    {
        return -1;
    }

    /* Create child task structure */
    task_t *child = (task_t *)kmalloc(sizeof(task_t));
    if (!child)
    {
        terminal_writestring("[FORK] ERROR: Failed to allocate child task\n");
        return -1;
    }

    /* Copy parent's entire task structure */
    memcpy(child, current_task, sizeof(task_t));

    /* Assign new PID */
    static uint32_t next_fork_pid = 100; /* Start fork PIDs at 100 */
    child->pid = next_fork_pid++;

    /* Update name */
    strcat(child->name, "-child");

    /* Set up parent-child relationship */
    task_add_child(current_task, child);

    /* Clone page directory (deep copy of memory) */
    child->page_directory = clone_page_directory(current_task->page_directory);
    if (!child->page_directory)
    {
        terminal_writestring("[FORK] ERROR: Failed to clone page directory\n");
        kfree(child);
        return -1;
    }

    /* Allocate new kernel stack */
    child->kernel_stack = (uint32_t)kmalloc(4096);
    if (!child->kernel_stack)
    {
        terminal_writestring("[FORK] ERROR: Failed to allocate kernel stack\n");
        /* TODO: free page directory */
        kfree(child);
        return -1;
    }

    /* NOTE: User stack is already cloned as part of page directory */

    /* Child starts in READY state */
    child->state = TASK_READY;
    child->exit_code = 0;
    child->waited = false;
    child->first_child = NULL;
    child->next_sibling = NULL;

    /* CRITICAL: Set child's EAX to 0 so it knows it's the child */
    child->context.eax = 0;

    /* Add to scheduler queue */
    extern void scheduler_add_task(task_t * task);
    scheduler_add_task(child);

    /* Return child PID to parent, 0 to child */
    return child->pid;
}

int sys_wait(int *status)
{
    if (!current_task)
    {
        return -1;
    }

    /* Check if we have any children */
    if (!current_task->first_child)
    {
        return -1; /* No children */
    }

    /* Look for zombie children */
    while (1)
    {
        task_t *child = current_task->first_child;

        while (child)
        {
            if (child->state == TASK_ZOMBIE && !child->waited)
            {
                /* Found a zombie child! */
                int pid = child->pid;
                int exit_code = child->exit_code;

                /* Mark as waited */
                child->waited = true;

                /* Copy exit status if pointer valid */
                if (status && (uint32_t)status < 0xC0000000)
                {
                    *status = exit_code;
                }

                /* Remove from children list and clean up */
                task_remove_child(current_task, child);

                /* TODO: Free child's memory properly */
                /* For now, just mark it for cleanup */

                return pid;
            }

            child = child->next_sibling;
        }

        /* No zombie children found - block and wait */
        task_block();
        task_yield();

        /* When we wake up, check again */
    }
}

int sys_exec(const char *path)
{
    /* Validate pointer */
    if ((uint32_t)path >= 0xC0000000)
    {
        return -1;
    }

    /* Open file */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }

    /* Read file into buffer (max 64KB for now) */
    void *elf_data = kmalloc(65536);
    if (!elf_data)
    {
        vfs_close(fd);
        return -1;
    }

    int bytes_read = vfs_read(fd, elf_data, 65536);
    vfs_close(fd);

    if (bytes_read <= 0)
    {
        kfree(elf_data);
        return -1;
    }

    /* If we're in kernel mode (shell), create a NEW user task */
    if (!current_task || current_task->ring == 0)
    {
        /* Create new user task */
        task_t *user_task = task_create_user(path, elf_data, 1);
        kfree(elf_data);

        if (!user_task)
        {
            return -1;
        }

        /* Add to scheduler */
        scheduler_add_task(user_task);

        /* Return the PID so caller can wait on it */
        return (int)user_task->pid;
    }

    /* If we're already in user mode, replace current process */
    /* Load ELF into current task's address space */
    if (!elf_load(current_task, elf_data))
    {
        kfree(elf_data);
        return -1;
    }

    kfree(elf_data);

    /* Setup user context to jump to new entry point */
    task_setup_user_context(current_task);

    /* Return 0 - but modified context means we'll jump to new program on syscall return */
    return 0;
}

/* ================================================================
 * SYSCALL DISPATCHER
 * ================================================================ */

void syscall_handler(struct registers *regs)
{
    uint32_t syscall_num = regs->eax;

    /* Bounds check */
    if (syscall_num >= SYSCALL_MAX)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("[SYSCALL] Invalid syscall number: ");
        terminal_write_dec(syscall_num);
        terminal_writestring("\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        regs->eax = (uint32_t)-1;
        return;
    }

    /* Dispatch */
    switch (syscall_num)
    {
    case SYS_EXIT:
        sys_exit((int)regs->ebx);
        /* Never returns */
        break;

    case SYS_WRITE:
        regs->eax = sys_write((const char *)regs->ebx);
        break;

    case SYS_READ:
        regs->eax = sys_read((char *)regs->ebx, regs->ecx);
        break;

    case SYS_YIELD:
        sys_yield();
        regs->eax = 0;
        break;

    case SYS_GETPID:
        regs->eax = sys_getpid();
        break;

    case SYS_SLEEP:
        sys_sleep(regs->ebx);
        regs->eax = 0;
        break;

    case SYS_FORK:
    {
        int child_pid = sys_fork();

        if (child_pid > 0)
        {
            /* Parent process */
            regs->eax = child_pid;
        }
        else if (child_pid == 0)
        {
            /* This shouldn't happen in parent */
            regs->eax = 0;
        }
        else
        {
            /* Error */
            regs->eax = (uint32_t)-1;
        }

        /* IMPORTANT: Child task needs EAX=0 when it starts */
        /* This will be set when the child is scheduled */
    }
    break;

    case SYS_EXEC:
        regs->eax = sys_exec((const char *)regs->ebx);
        break;

    case SYS_WAIT:
        regs->eax = sys_wait((int *)regs->ebx);
        break;

    default:
        regs->eax = (uint32_t)-1;
        break;
    }
}

/* ================================================================
 * INITIALIZATION
 * ================================================================ */

void syscall_init(void)
{
    /* Install INT 0x80 - DPL=3 for user mode access */
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, 0xEE);

    terminal_writestring("[SYSCALL] System call interface initialized\n");
    terminal_writestring("[SYSCALL] Available: exit, write, read, yield, getpid, sleep, fork, exec, wait\n");
}