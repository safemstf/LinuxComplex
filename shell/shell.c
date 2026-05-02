/* shell/shell.c - Interactive command shell
 *
 * CLEANED: Test tasks moved to test_tasks.c
 */

#include "../drivers/terminal.h"
#include "../kernel/kernel.h"
#include "../mm/pmm.h"
#include "../interrupts/pagefault.h"
#include "../kernel/scheduler.h"
#include "../kernel/task.h"
#include "test_tasks.h"
#include "../fs/vfs.h"
#include "../drivers/ata.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_PROMPT "complex> "

static char command_buffer[SHELL_BUFFER_SIZE];
static size_t command_pos = 0;

#define HISTORY_SIZE 10
static char history[HISTORY_SIZE][SHELL_BUFFER_SIZE] __attribute__((unused));
static size_t history_count = 0;

extern char keyboard_buffer_pop(void);
extern bool keyboard_has_data(void);

static void cmd_forktest(void);
static void cmd_waitdemo(void);
static void cmd_syscalltest(void);
static void test_child_task(void);

static void shell_display_prompt(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring(SHELL_PROMPT);
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

static void cmd_help(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║           OSComplex - Available Commands                ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    terminal_writestring("System Commands:\n");
    terminal_writestring("  help             - Show this help message\n");
    terminal_writestring("  clear            - Clear the screen\n");
    terminal_writestring("  about            - About OSComplex\n");
    terminal_writestring("  halt             - Shutdown the system\n");

    terminal_writestring("\nMemory & System:\n");
    terminal_writestring("  meminfo          - Show physical memory usage\n");
    terminal_writestring("  sysinfo          - Show system information\n");
    terminal_writestring("  testpf           - Test page fault handling\n");
    terminal_writestring("  heaptest         - Test heap allocator\n");

    terminal_writestring("\nTask & Scheduler:\n");
    terminal_writestring("  ps               - List all running tasks\n");
    terminal_writestring("  sched            - Show scheduler statistics\n");
    terminal_writestring("  spawn            - Spawn test tasks\n");

    terminal_writestring("\nFile System:\n");
    terminal_writestring("  ls [path]        - List directory contents\n");
    terminal_writestring("  pwd              - Print current directory\n");
    terminal_writestring("  cd [path]        - Change directory\n");
    terminal_writestring("  mkdir <name>     - Create a directory\n");
    terminal_writestring("  rmdir <name>     - Remove a directory\n");
    terminal_writestring("  touch <file>     - Create an empty file\n");
    terminal_writestring("  cat <file>       - Display file contents\n");
    terminal_writestring("  rm <file>        - Delete a file\n");

    terminal_writestring("\nDisk Commands:\n");
    terminal_writestring("  diskinfo         - Show disk information\n");
    terminal_writestring("  readsector <n>   - Read sector at LBA n\n");
    terminal_writestring("  writesector <n> <text>\n");
    terminal_writestring("                   - Write text to sector n\n");

    terminal_writestring("\nUser Mode:\n");
    terminal_writestring("  usertest         - Test user-mode execution\n");
    terminal_writestring("  exec <program>   - Execute a user program\n");

    terminal_writestring("  forktest      - Test fork/wait functionality\n");
    terminal_writestring("  waitdemo      - Show child process states\n");
    terminal_writestring("  syscalltest   - Test system call infrastructure\n");

    terminal_writestring("\nUtilities:\n");
    terminal_writestring("  echo <text>      - Print text to screen\n");
    terminal_writestring("  echo <text> > <file>\n");
    terminal_writestring("                   - Write text to a file\n");
    terminal_writestring("  ai               - Show AI learning statistics\n");
}

static void cmd_about(void)
{
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
    terminal_writestring("╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║              OSComplex v0.1-alpha                        ║\n");
    terminal_writestring("║           An AI-Native Operating System                 ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("🤖 First OS with AI embedded at the kernel level\n");
    terminal_writestring("🧠 Learns from your usage patterns\n");
    terminal_writestring("⚡ Built from scratch in C and Assembly\n");
    terminal_writestring("🎯 Designed for the future of computing\n");
    terminal_writestring("🔄 Now with multitasking support!\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("Status: Experimental | Learning: Active\n");
    terminal_writestring("Architecture: i686 (32-bit)\n\n");
}

static void cmd_echo(const char *args)
{
    terminal_writestring(args);
    terminal_writestring("\n");
}

static void cmd_meminfo(void)
{
    uint32_t total = pmm_get_total_blocks();
    uint32_t used = pmm_get_used_blocks();
    uint32_t free = pmm_get_free_blocks();

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║               Physical Memory Information               ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("  Total blocks : ");
    terminal_write_dec(total);
    terminal_writestring("\n  Used blocks  : ");
    terminal_write_dec(used);
    terminal_writestring("\n  Free blocks  : ");
    terminal_write_dec(free);
    terminal_writestring("\n\n  Block size   : ");
    terminal_write_dec(PAGE_SIZE);
    terminal_writestring(" bytes\n\n");
}

static void cmd_sysinfo(void)
{
    uint32_t total = pmm_get_total_blocks();
    uint32_t used = pmm_get_used_blocks();
    uint32_t free = pmm_get_free_blocks();

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║              System Information                         ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("Physical Memory:\n");
    terminal_writestring("  Total    : ");
    terminal_write_dec(total * 4);
    terminal_writestring(" KB (");
    terminal_write_dec(total);
    terminal_writestring(" blocks)\n  Used     : ");
    terminal_write_dec(used * 4);
    terminal_writestring(" KB (");
    terminal_write_dec(used);
    terminal_writestring(" blocks)\n  Free     : ");
    terminal_write_dec(free * 4);
    terminal_writestring(" KB (");
    terminal_write_dec(free);
    terminal_writestring(" blocks)\n");

    terminal_writestring("\nMemory Layout:\n");
    terminal_writestring("  Kernel   : 0x00100000 - 0x08000000 (127 MB)\n");
    terminal_writestring("  Heap     : 0xC0400000 - 0xC0800000 (4 MB)\n");

    terminal_writestring("\nSubsystems Status:\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("  [✓] PMM        - Physical Memory Manager\n");
    terminal_writestring("  [✓] VMM        - Virtual Memory Manager\n");
    terminal_writestring("  [✓] Heap       - Kernel Heap Allocator\n");
    terminal_writestring("  [✓] IDT        - Interrupt Descriptor Table\n");
    terminal_writestring("  [✓] PIC        - Programmable Interrupt Controller\n");
    terminal_writestring("  [✓] Timer      - Programmable Interval Timer\n");
    terminal_writestring("  [✓] Task       - Process Management\n");
    terminal_writestring("  [✓] Scheduler  - Round-Robin Scheduler\n");
    terminal_writestring("  [✓] Syscall    - System Call Interface\n");
    terminal_writestring("  [✓] AI         - Learning System\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("\n");
}

static void cmd_testpf(void) { test_page_fault_recovery(); }

static void cmd_heaptest(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║              Heap Allocator Test                        ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    terminal_writestring("[TEST 1] Small allocations (64 bytes each)...\n");
    void *ptr1 = kmalloc(64), *ptr2 = kmalloc(64), *ptr3 = kmalloc(64);

    if (ptr1 && ptr2 && ptr3)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("         ✓ All allocations successful\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_writestring("         ptr1 = 0x");
        terminal_write_hex((uint32_t)ptr1);
        terminal_writestring("\n         ptr2 = 0x");
        terminal_write_hex((uint32_t)ptr2);
        terminal_writestring("\n         ptr3 = 0x");
        terminal_write_hex((uint32_t)ptr3);
        terminal_writestring("\n");
    }
    else
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("         ✗ Allocation failed\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    terminal_writestring("\n[TEST 2] Writing to allocated memory...\n");
    uint32_t *test = (uint32_t *)ptr1;
    *test = 0x12345678;

    if (*test == 0x12345678)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("         ✓ Memory write/read successful\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_writestring("         Value = 0x");
        terminal_write_hex(*test);
        terminal_writestring("\n");
    }

    terminal_writestring("\n[TEST 3] Freeing memory...\n");
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("         ✓ Memory freed successfully\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("\n[TEST 4] Large allocation (8KB)...\n");
    void *large = kmalloc(8192);

    if (large)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("         ✓ Large allocation successful\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_writestring("         ptr = 0x");
        terminal_write_hex((uint32_t)large);
        terminal_writestring("\n");
        kfree(large);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("         ✓ Large memory freed\n");
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("\n");
}

static void cmd_ps(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║                    Task List                             ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("PID  STATE    NAME\n");
    terminal_writestring("---  -------  --------------------\n");

    task_t *task = kernel_task;
    if (!task)
    {
        terminal_writestring("(no tasks)\n\n");
        return;
    }

    task_t *start = task; // Remember where we started
    do
    {
        char buf[16];
        itoa(task->pid, buf);
        terminal_writestring(buf);
        terminal_writestring(task->pid < 10 ? "    " : (task->pid < 100 ? "   " : "  "));

        switch (task->state)
        {
        case TASK_READY:
            terminal_writestring("READY  ");
            break;
        case TASK_RUNNING:
            terminal_writestring("RUN    ");
            break;
        case TASK_BLOCKED:
            terminal_writestring("BLOCK  ");
            break;
        case TASK_SLEEPING:
            terminal_writestring("SLEEP  ");
            break;
        case TASK_ZOMBIE:
            terminal_writestring("ZOMBIE ");
            break;
        default:
            terminal_writestring("???    ");
            break;
        }

        terminal_writestring(" ");
        terminal_writestring(task->name);
        terminal_writestring("\n");

        task = task->next;
    } while (task && task != start); // ← STOP when we loop back!

    terminal_writestring("\n");
}

static void cmd_sched(void)
{
    scheduler_stats_t stats = scheduler_get_stats();

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║               Scheduler Statistics                       ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    char buf[16];
    terminal_writestring("Total tasks       : ");
    itoa(stats.total_tasks, buf);
    terminal_writestring(buf);
    terminal_writestring("\n");
    terminal_writestring("Ready tasks       : ");
    itoa(stats.ready_tasks, buf);
    terminal_writestring(buf);
    terminal_writestring("\n");
    terminal_writestring("Blocked tasks     : ");
    itoa(stats.blocked_tasks, buf);
    terminal_writestring(buf);
    terminal_writestring("\n");
    terminal_writestring("Context switches  : ");
    itoa(stats.context_switches, buf);
    terminal_writestring(buf);
    terminal_writestring("\n");
    terminal_writestring("Total ticks       : ");
    itoa(stats.total_ticks, buf);
    terminal_writestring(buf);
    terminal_writestring("\n\n");
}

static void cmd_spawn(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n[SPAWN] Creating test tasks...\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    task_t *t1 = task_create("syscall_test", syscall_test_task, 10);
    if (t1)
        scheduler_add_task(t1);

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[SPAWN] Syscall test task created!\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("\n");
}

static void cmd_halt(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║                System Shutdown Initiated                ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("Goodbye! System halted.\n(Close QEMU window or press Ctrl+C)\n\n");

    __asm__ volatile("cli; hlt");
}

/* ====================================================================
 * ls - List directory with colors and file sizes
 * ==================================================================== */

static void cmd_ls(const char *path)
{
    vfs_node_t *dir;

    /* Use current directory if no path specified */
    if (!path || !*path)
    {
        dir = vfs_cwd;
        path = ".";
    }
    else
    {
        dir = vfs_resolve_path(path);
    }

    if (!dir)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("ls: ");
        terminal_writestring(path);
        terminal_writestring(": No such file or directory\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    if (dir->type != VFS_DIRECTORY)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("ls: ");
        terminal_writestring(path);
        terminal_writestring(": Not a directory\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    if (!dir->ops || !dir->ops->readdir)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("ls: filesystem does not support listing\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    terminal_writestring("\n");

    /* Read and display directory entries */
    bool empty = true;
    for (uint32_t i = 0;; i++)
    {
        dirent_t *ent = dir->ops->readdir(dir, i);
        if (!ent)
            break;

        empty = false;

        /* Color by type */
        if (ent->type == VFS_DIRECTORY)
        {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
        }
        else
        {
            terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        }

        terminal_writestring(ent->name);

        /* Add slash for directories */
        if (ent->type == VFS_DIRECTORY)
        {
            terminal_writestring("/");
        }

        terminal_writestring("  ");
    }

    if (empty)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        terminal_writestring("(empty)");
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    terminal_writestring("\n\n");
}

/* ====================================================================
 * cat - Display file contents
 * ==================================================================== */

static void cmd_cat(const char *path)
{
    if (!path || !*path)
    {
        terminal_writestring("cat: missing file operand\n");
        return;
    }

    /* Open file */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("cat: ");
        terminal_writestring(path);
        terminal_writestring(": No such file or directory\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    /* Read and display file contents */
    char buffer[256];
    int bytes_read;

    terminal_writestring("\n");

    while ((bytes_read = vfs_read(fd, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[bytes_read] = '\0'; /* Null terminate */
        terminal_writestring(buffer);
    }

    terminal_writestring("\n");

    vfs_close(fd);
}

/* ====================================================================
 * echo - Write text to file (echo "text" > file)
 * ==================================================================== */

static void cmd_echo_to_file(const char *text, const char *filename)
{
    if (!filename || !*filename)
    {
        terminal_writestring("echo: missing filename\n");
        return;
    }

    /* Create or truncate file */
    int fd = vfs_open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("echo: cannot create ");
        terminal_writestring(filename);
        terminal_writestring("\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    /* Write text to file */
    if (text && *text)
    {
        vfs_write(fd, text, strlen(text));
    }

    vfs_close(fd);

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("✓ Wrote to ");
    terminal_writestring(filename);
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ====================================================================
 * rm - Remove file
 * ==================================================================== */

static void cmd_rm(const char *path)
{
    if (!path || !*path)
    {
        terminal_writestring("rm: missing file operand\n");
        return;
    }

    if (vfs_unlink(path) < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("rm: cannot remove '");
        terminal_writestring(path);
        terminal_writestring("'\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("✓ Removed ");
    terminal_writestring(path);
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ====================================================================
 * mkdir - Create directory
 * ==================================================================== */

static void cmd_mkdir(const char *path)
{
    if (!path || !*path)
    {
        terminal_writestring("mkdir: missing operand\n");
        return;
    }

    if (vfs_mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("mkdir: cannot create directory '");
        terminal_writestring(path);
        terminal_writestring("'\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("✓ Created ");
    terminal_writestring(path);
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ====================================================================
 * rmdir - Remove directory
 * ==================================================================== */

static void cmd_rmdir(const char *path)
{
    if (!path || !*path)
    {
        terminal_writestring("rmdir: missing operand\n");
        return;
    }

    if (vfs_rmdir(path) < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("rmdir: failed to remove '");
        terminal_writestring(path);
        terminal_writestring("': Directory not empty or does not exist\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("✓ Removed directory ");
    terminal_writestring(path);
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ====================================================================
 * pwd - Print working directory
 * ==================================================================== */

static void cmd_pwd(void)
{
    const char *cwd = vfs_getcwd();
    if (cwd)
    {
        if (cwd[0] == '\0')
        {
            terminal_writestring("/\n");
        }
        else
        {
            terminal_writestring("/");
            terminal_writestring(cwd);
            terminal_writestring("\n");
        }
    }
}

/* ====================================================================
 * cd - Change directory
 * ==================================================================== */

static void cmd_cd(const char *path)
{
    if (!path || !*path)
    {
        /* No argument: go to root */
        path = "/";
    }

    if (vfs_chdir(path) < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("cd: ");
        terminal_writestring(path);
        terminal_writestring(": No such directory\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    }
}

/* ====================================================================
 * touch - Create empty file
 * ==================================================================== */

static void cmd_touch(const char *path)
{
    if (!path || !*path)
    {
        terminal_writestring("touch: missing file operand\n");
        return;
    }

    int fd = vfs_open(path, O_WRONLY | O_CREAT);
    if (fd < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("touch: cannot create '");
        terminal_writestring(path);
        terminal_writestring("'\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    vfs_close(fd);

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("✓ Created ");
    terminal_writestring(path);
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ====================================================================
 * diskinfo - Show detected ATA drives
 * ==================================================================== */

static void cmd_diskinfo(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║                  ATA Drive Information                  ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    const char *drive_names[] = {
        "Primary Master",
        "Primary Slave",
        "Secondary Master",
        "Secondary Slave"};

    for (uint8_t i = 0; i < 4; i++)
    {
        ata_drive_info_t *info = ata_get_drive_info(i);

        terminal_writestring(drive_names[i]);
        terminal_writestring(": ");

        if (info && info->present)
        {
            if (info->is_atapi)
            {
                terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK));
                terminal_writestring("ATAPI device (not supported)\n");
                terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
            }
            else
            {
                terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                terminal_writestring("PRESENT\n");
                terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

                terminal_writestring("  Model    : ");
                terminal_writestring(info->model);
                terminal_writestring("\n");

                terminal_writestring("  Serial   : ");
                terminal_writestring(info->serial);
                terminal_writestring("\n");

                terminal_writestring("  Firmware : ");
                terminal_writestring(info->firmware);
                terminal_writestring("\n");

                terminal_writestring("  Sectors  : ");
                terminal_write_dec(info->sectors);
                terminal_writestring(" (");
                terminal_write_dec(info->sectors / 2048);
                terminal_writestring(" MB)\n");
            }
        }
        else
        {
            terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            terminal_writestring("Not present\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        }

        terminal_writestring("\n");
    }
}

/* ====================================================================
 * readsector - Read and display a sector
 * ==================================================================== */

static void cmd_readsector(const char *args)
{
    if (!args || !*args)
    {
        terminal_writestring("Usage: readsector <lba>\n");
        return;
    }

    /* Parse LBA (simple decimal for now) */
    uint32_t lba = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9')
    {
        lba = lba * 10 + (*p - '0');
        p++;
    }

    /* Allocate sector buffer */
    uint8_t *buffer = (uint8_t *)kmalloc(512);
    if (!buffer)
    {
        terminal_writestring("Error: Out of memory\n");
        return;
    }

    /* Read sector */
    terminal_writestring("Reading sector ");
    terminal_write_dec(lba);
    terminal_writestring(" from Primary Master...\n");

    if (ata_read_sector(ATA_PRIMARY_MASTER, lba, buffer) < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("Error: Failed to read sector\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kfree(buffer);
        return;
    }

    /* Display first 256 bytes in hex dump format */
    terminal_writestring("\n");
    for (int i = 0; i < 256; i += 16)
    {
        /* Offset */
        terminal_write_hex(i);
        terminal_writestring(": ");

        /* Hex bytes */
        for (int j = 0; j < 16; j++)
        {
            uint8_t byte = buffer[i + j];
            char hex[3];
            hex[0] = "0123456789ABCDEF"[byte >> 4];
            hex[1] = "0123456789ABCDEF"[byte & 0x0F];
            hex[2] = '\0';
            terminal_writestring(hex);
            terminal_writestring(" ");
        }

        terminal_writestring(" ");

        /* ASCII representation */
        for (int j = 0; j < 16; j++)
        {
            uint8_t byte = buffer[i + j];
            if (byte >= 32 && byte <= 126)
            {
                terminal_putchar(byte);
            }
            else
            {
                terminal_putchar('.');
            }
        }

        terminal_writestring("\n");
    }

    terminal_writestring("...\n\n");

    kfree(buffer);
}

/* ====================================================================
 * writesector - Write data to a sector
 * ==================================================================== */

static void cmd_writesector(const char *args)
{
    if (!args || !*args)
    {
        terminal_writestring("Usage: writesector <lba> <text>\n");
        return;
    }

    /* Parse LBA */
    uint32_t lba = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9')
    {
        lba = lba * 10 + (*p - '0');
        p++;
    }

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t')
        p++;

    if (!*p)
    {
        terminal_writestring("Usage: writesector <lba> <text>\n");
        return;
    }

    /* Allocate sector buffer and fill with zeros */
    uint8_t *buffer = (uint8_t *)kmalloc(512);
    if (!buffer)
    {
        terminal_writestring("Error: Out of memory\n");
        return;
    }
    memset(buffer, 0, 512);

    /* Copy text to buffer (up to 512 bytes) */
    size_t len = 0;
    while (*p && len < 512)
    {
        buffer[len++] = *p++;
    }

    /* Write sector */
    terminal_writestring("Writing to sector ");
    terminal_write_dec(lba);
    terminal_writestring(" on Primary Master...\n");

    if (ata_write_sector(ATA_PRIMARY_MASTER, lba, buffer) < 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("Error: Failed to write sector\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kfree(buffer);
        return;
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("✓ Sector written successfully\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    kfree(buffer);
}

void cmd_exec(const char *args)
{
    if (!args || !*args)
    {
        terminal_writestring("Usage: exec <program>\n");
        terminal_writestring("Example: exec bin/hello\n");
        return;
    }

    /* Skip leading spaces */
    while (*args == ' ')
        args++;

    /* Call sys_exec to create and add the task - returns PID on success */
    int child_pid = sys_exec(args);

    if (child_pid <= 0)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("exec: ");
        terminal_writestring(args);
        terminal_writestring(": not found\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    /* Get the child task structure */
    task_t *child = task_find_by_pid((uint32_t)child_pid);
    if (!child)
    {
        return;
    }

    /* Wait for child to exit */
    while (child->state != TASK_ZOMBIE)
    {
        scheduler_schedule();  /* Let child run, possibly multiple cycles */
    }

    /* Extra scheduler cycle to ensure CPU stack is no longer on child */
    scheduler_schedule();

    /* Safe to destroy child now */
    task_destroy(child);
}

extern void user_main(void);

static void cmd_usertest(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n[USERMODE] Testing Ring 3 system calls...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    /* Allocate kernel stack for TSS */
    void *kernel_stack = kmalloc(4096);
    if (!kernel_stack)
    {
        terminal_writestring("[ERROR] Failed to allocate kernel stack\n");
        return;
    }

    terminal_writestring("[DEBUG] Kernel stack allocated at: 0x");
    terminal_write_hex((uint32_t)kernel_stack);
    terminal_writestring("\n");

    uint32_t esp0 = (uint32_t)kernel_stack + 4096;
    terminal_writestring("[DEBUG] Setting TSS.ESP0 to: 0x");
    terminal_write_hex(esp0);
    terminal_writestring("\n");

    tss_set_kernel_stack(esp0);

    /* User code at 0x10000000 (256MB - well below kernel at 0xC0000000) */
    uint32_t user_code_addr = 0x10000000;
    void *phys_code = pmm_alloc_block();
    if (!phys_code)
    {
        terminal_writestring("[ERROR] Failed to allocate physical page for code\n");
        kfree(kernel_stack);
        return;
    }

    terminal_writestring("[DEBUG] Physical page for code: 0x");
    terminal_write_hex((uint32_t)phys_code);
    terminal_writestring("\n");

    vmm_map_page(user_code_addr, (uint32_t)phys_code, VMM_PRESENT | VMM_WRITE | VMM_USER);

    terminal_writestring("[DEBUG] Mapped code page: virt 0x");
    terminal_write_hex(user_code_addr);
    terminal_writestring(" -> phys 0x");
    terminal_write_hex((uint32_t)phys_code);
    terminal_writestring("\n");

    /* User stack at 0x20000000 (512MB - different region from code) */
    uint32_t user_stack_addr = 0x20000000;
    void *phys_stack = pmm_alloc_block();
    if (!phys_stack)
    {
        terminal_writestring("[ERROR] Failed to allocate physical page for stack\n");
        vmm_unmap_page(user_code_addr);
        pmm_free_block(phys_code);
        kfree(kernel_stack);
        return;
    }

    terminal_writestring("[DEBUG] Physical page for stack: 0x");
    terminal_write_hex((uint32_t)phys_stack);
    terminal_writestring("\n");

    vmm_map_page(user_stack_addr, (uint32_t)phys_stack, VMM_PRESENT | VMM_WRITE | VMM_USER);

    terminal_writestring("[DEBUG] Mapped stack page: virt 0x");
    terminal_write_hex(user_stack_addr);
    terminal_writestring(" -> phys 0x");
    terminal_write_hex((uint32_t)phys_stack);
    terminal_writestring("\n");

    /* Zero both pages */
    memset((void *)user_code_addr, 0, PAGE_SIZE);
    memset((void *)user_stack_addr, 0, PAGE_SIZE);

    /* User code: syscall then infinite loop (NOT hlt!)
     *
     * This code does:
     *   mov eax, 0      ; SYS_EXIT
     *   mov ebx, 42     ; exit code
     *   int 0x80        ; syscall
     *   jmp $           ; infinite loop (NOT hlt - that's privileged!)
     */
    static uint8_t user_code[] = {
        0xB8, 0x00, 0x00, 0x00, 0x00, /* mov eax, 0 */
        0xBB, 0x2A, 0x00, 0x00, 0x00, /* mov ebx, 42 */
        0xCD, 0x80,                   /* int 0x80 */
        0xEB, 0xFE                    /* jmp $ */
    };

    memcpy((void *)user_code_addr, user_code, sizeof(user_code));

    terminal_writestring("[DEBUG] Copied ");
    terminal_write_dec(sizeof(user_code));
    terminal_writestring(" bytes of user code\n");

    terminal_writestring("[DEBUG] User code entry point: 0x");
    terminal_write_hex(user_code_addr);
    terminal_writestring("\n");

    terminal_writestring("[DEBUG] User stack pointer: 0x");
    terminal_write_hex(user_stack_addr + PAGE_SIZE);
    terminal_writestring("\n");

    terminal_writestring("\n[DEBUG] Verifying page mappings...\n");
    if (!vmm_is_mapped(user_code_addr))
    {
        terminal_writestring("[ERROR] Code page not mapped!\n");
        return;
    }
    terminal_writestring("[DEBUG] ✓ Code page is mapped\n");

    if (!vmm_is_mapped(user_stack_addr))
    {
        terminal_writestring("[ERROR] Stack page not mapped!\n");
        return;
    }
    terminal_writestring("[DEBUG] ✓ Stack page is mapped\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\n[DEBUG] Everything ready! Entering Ring 3...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    /* Jump to user mode - stack pointer at TOP of stack page */
    enter_usermode(user_code_addr, user_stack_addr + PAGE_SIZE);

    /* If we get here, we returned from user mode! */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║          SUCCESS! RETURNED FROM RING 3!                  ║\n");
    terminal_writestring("║      System call interface is working!                   ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    /* Cleanup */
    vmm_unmap_page(user_code_addr);
    vmm_unmap_page(user_stack_addr);
    pmm_free_block(phys_code);
    pmm_free_block(phys_stack);
    kfree(kernel_stack);
}

/* Command: forktest - Test fork() and wait() */
static void cmd_forktest(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║              Fork/Wait Test                              ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    terminal_writestring("This command demonstrates fork() and wait() system calls\n");
    terminal_writestring("in kernel mode (simulated).\n\n");

    terminal_writestring("Creating child task...\n");

    /* Create a child task that will exit after some work */
    task_t *child = task_create("test-child", test_child_task, 1);
    if (!child)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("ERROR: Failed to create child task\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        return;
    }

    terminal_writestring("Child task created with PID ");
    terminal_write_dec(child->pid);
    terminal_writestring("\n");

    terminal_writestring("\nNote: For true fork() testing, you need to:\n");
    terminal_writestring("1. Load a user-mode program\n");
    terminal_writestring("2. That program calls fork() via INT 0x80\n");
    terminal_writestring("3. Parent calls wait() to get child's exit status\n\n");

    /* Add child to scheduler */
    scheduler_add_task(child);

    terminal_writestring("Child added to scheduler. It will run soon.\n");
    terminal_writestring("Use 'ps' to see the task list.\n\n");
}

/* Helper task for forktest */
static void test_child_task(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("\n[CHILD TASK] Running! PID=");
    terminal_write_dec(current_task->pid);
    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    for (int i = 0; i < 5; i++)
    {
        terminal_writestring("[CHILD] Iteration ");
        terminal_write_dec(i);
        terminal_writestring("\n");
        task_sleep(100); /* Sleep 100ms */
    }

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[CHILD TASK] Exiting with code 42\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    task_exit(42);
}

/* Command: waitdemo - Demonstrate wait() functionality */
static void cmd_waitdemo(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║              Wait() Demonstration                        ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    terminal_writestring("Current task PID: ");
    terminal_write_dec(current_task->pid);
    terminal_writestring("\n\n");

    /* Check if current task has children */
    if (!current_task->first_child)
    {
        terminal_writestring("This task has no children.\n");
        terminal_writestring("Use 'forktest' to create a child task first.\n\n");
        return;
    }

    terminal_writestring("Children of this task:\n");
    task_t *child = current_task->first_child;
    while (child)
    {
        terminal_writestring("  PID ");
        terminal_write_dec(child->pid);
        terminal_writestring(" - ");
        terminal_writestring(child->name);
        terminal_writestring(" [");
        switch (child->state)
        {
        case TASK_READY:
            terminal_writestring("READY");
            break;
        case TASK_RUNNING:
            terminal_writestring("RUNNING");
            break;
        case TASK_BLOCKED:
            terminal_writestring("BLOCKED");
            break;
        case TASK_SLEEPING:
            terminal_writestring("SLEEPING");
            break;
        case TASK_ZOMBIE:
            terminal_writestring("ZOMBIE");
            break;
        default:
            terminal_writestring("UNKNOWN");
            break;
        }
        terminal_writestring("]\n");

        if (child->state == TASK_ZOMBIE)
        {
            terminal_writestring("    Exit code: ");
            terminal_write_dec(child->exit_code);
            terminal_writestring("\n");
        }

        child = child->next_sibling;
    }

    terminal_writestring("\nNote: In user mode, parent would call wait() to reap zombies.\n\n");
}

/* Command: syscalltest - Test system call infrastructure */
static void cmd_syscalltest(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║           System Call Test                               ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    terminal_writestring("Available system calls:\n");
    terminal_writestring("  0 - SYS_EXIT    Exit current process\n");
    terminal_writestring("  1 - SYS_WRITE   Write string to terminal\n");
    terminal_writestring("  2 - SYS_READ    Read from keyboard (TODO)\n");
    terminal_writestring("  3 - SYS_YIELD   Yield CPU to other tasks\n");
    terminal_writestring("  4 - SYS_GETPID  Get current process ID\n");
    terminal_writestring("  5 - SYS_SLEEP   Sleep for N milliseconds\n");
    terminal_writestring("  6 - SYS_FORK    Create child process\n");
    terminal_writestring("  7 - SYS_EXEC    Execute new program\n");
    terminal_writestring("  8 - SYS_WAIT    Wait for child to exit\n\n");

    terminal_writestring("Testing SYS_GETPID...\n");

    /* Simulate syscall by calling the function directly */
    uint32_t pid = sys_getpid();

    terminal_writestring("Current PID: ");
    terminal_write_dec(pid);
    terminal_writestring("\n\n");

    terminal_writestring("To test fork/wait/exec properly, you need:\n");
    terminal_writestring("1. A user-mode program that makes INT 0x80 calls\n");
    terminal_writestring("2. Compile it as an ELF binary\n");
    terminal_writestring("3. Load it with 'exec <program>'\n\n");
}

static void shell_execute_command(const char *cmd)
{
    if (!cmd || !*cmd)
        return;

    bool success = false;
    const char *args = cmd;
    while (*args && *args != ' ')
        args++;
    if (*args == ' ')
        args++;

    /* System commands */
    if (strcmp(cmd, "help") == 0)
    {
        cmd_help();
        success = true;
    }
    else if (strcmp(cmd, "clear") == 0)
    {
        terminal_clear();
        success = true;
    }
    else if (strcmp(cmd, "about") == 0)
    {
        cmd_about();
        success = true;
    }
    else if (strcmp(cmd, "halt") == 0)
    {
        cmd_halt();
    }

    /* Memory commands */
    else if (strcmp(cmd, "meminfo") == 0)
    {
        cmd_meminfo();
        success = true;
    }
    else if (strcmp(cmd, "sysinfo") == 0)
    {
        cmd_sysinfo();
        success = true;
    }
    else if (strcmp(cmd, "testpf") == 0)
    {
        cmd_testpf();
        success = true;
    }
    else if (strcmp(cmd, "heaptest") == 0)
    {
        cmd_heaptest();
        success = true;
    }

    /* Task commands */
    else if (strcmp(cmd, "ps") == 0)
    {
        cmd_ps();
        success = true;
    }
    else if (strcmp(cmd, "sched") == 0)
    {
        cmd_sched();
        success = true;
    }
    else if (strcmp(cmd, "spawn") == 0)
    {
        cmd_spawn();
        success = true;
    }

    /* AI command */
    else if (strcmp(cmd, "ai") == 0)
    {
        ai_show_stats();
        success = true;
    }

    /* Echo command with redirect support */
    else if (strncmp(cmd, "echo ", 5) == 0)
    {
        /* Check for > redirect */
        char *redirect = strstr(args, ">");
        if (redirect)
        {
            /* Make a working copy since we'll modify it */
            char args_copy[256];
            strncpy(args_copy, args, sizeof(args_copy) - 1);
            args_copy[sizeof(args_copy) - 1] = '\0';

            /* Find > in the copy */
            redirect = strstr(args_copy, ">");
            if (redirect)
            {
                /* Split at > */
                *redirect = '\0';

                /* Get text (before >) and filename (after >) */
                char *text = args_copy;
                char *filename = redirect + 1;

                /* Trim leading whitespace from filename */
                while (*filename == ' ' || *filename == '\t')
                    filename++;

                /* Trim trailing whitespace from text */
                char *text_end = redirect - 1;
                while (text_end > text && (*text_end == ' ' || *text_end == '\t'))
                {
                    *text_end = '\0';
                    text_end--;
                }

                /* Remove quotes from text if present */
                if (text[0] == '"' || text[0] == '\'')
                {
                    text++;
                    size_t len = strlen(text);
                    if (len > 0 && (text[len - 1] == '"' || text[len - 1] == '\''))
                    {
                        text[len - 1] = '\0';
                    }
                }

                cmd_echo_to_file(text, filename);
            }
            else
            {
                cmd_echo(args);
            }
        }
        else
        {
            /* No redirect - just print to screen */
            cmd_echo(args);
        }
        success = true;
    }

    /* File system commands */
    else if (strcmp(cmd, "ls") == 0)
    {
        cmd_ls(NULL);
        success = true;
    }
    else if (strncmp(cmd, "ls ", 3) == 0)
    {
        cmd_ls(args);
        success = true;
    }
    else if (strcmp(cmd, "pwd") == 0)
    {
        cmd_pwd();
        success = true;
    }
    else if (strcmp(cmd, "cd") == 0)
    {
        cmd_cd("/");
        success = true;
    }
    else if (strncmp(cmd, "cd ", 3) == 0)
    {
        cmd_cd(args);
        success = true;
    }
    else if (strncmp(cmd, "mkdir ", 6) == 0)
    {
        cmd_mkdir(args);
        success = true;
    }
    else if (strncmp(cmd, "rmdir ", 6) == 0)
    {
        cmd_rmdir(args);
        success = true;
    }
    else if (strncmp(cmd, "cat ", 4) == 0)
    {
        cmd_cat(args);
        success = true;
    }
    else if (strncmp(cmd, "rm ", 3) == 0)
    {
        cmd_rm(args);
        success = true;
    }
    else if (strncmp(cmd, "touch ", 6) == 0)
    {
        cmd_touch(args);
        success = true;
    }

    /* Disk commands */
    else if (strcmp(cmd, "diskinfo") == 0)
    {
        cmd_diskinfo();
        success = true;
    }
    else if (strncmp(cmd, "readsector ", 11) == 0)
    {
        cmd_readsector(args);
        success = true;
    }
    else if (strncmp(cmd, "writesector ", 12) == 0)
    {
        cmd_writesector(args);
        success = true;
    }
    else if (strcmp(cmd, "usertest") == 0)
    {
        cmd_usertest();
        success = true;
    }

    // user commands
    else if (strncmp(cmd, "exec ", 5) == 0)
    {
        cmd_exec(args);
        success = true;
    }

    else if (strcmp(cmd, "forktest") == 0)
    {
        cmd_forktest();
        success = true;
    }
    else if (strcmp(cmd, "waitdemo") == 0)
    {
        cmd_waitdemo();
        success = true;
    }
    else if (strcmp(cmd, "syscalltest") == 0)
    {
        cmd_syscalltest();
        success = true;
    }

    /* Unknown command */
    else
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("Unknown command: ");
        terminal_writestring(cmd);
        terminal_writestring("\n");

        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_writestring("Type 'help' for available commands\n");

        const char *suggestion = ai_predict_command(cmd);
        if (suggestion)
        {
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            terminal_writestring("[AI] Did you mean: ");
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            terminal_writestring(suggestion);
            terminal_writestring("?\n");
        }
    }

    ai_learn_command(cmd, success);
}

static void shell_process_input(void)
{
    if (!keyboard_has_data())
        return;

    char c = keyboard_buffer_pop();
    if (!c)
        return;

    switch (c)
    {
    case '\n':
        terminal_putchar('\n');
        command_buffer[command_pos] = '\0';
        shell_execute_command(command_buffer);
        command_pos = 0;
        shell_display_prompt();
        break;

    case '\b':
        if (command_pos > 0)
        {
            command_pos--;
            terminal_putchar('\b');
        }
        break;

    case '\t':
        command_buffer[command_pos] = '\0';
        ai_show_suggestions(command_buffer);
        shell_display_prompt();
        for (size_t i = 0; i < command_pos; i++)
            terminal_putchar(command_buffer[i]);
        break;

    default:
        if (command_pos < SHELL_BUFFER_SIZE - 1)
        {
            command_buffer[command_pos++] = c;
            terminal_putchar(c);
        }
        break;
    }
}

void shell_init(void)
{
    command_pos = 0;
    history_count = 0;
    memset(command_buffer, 0, sizeof(command_buffer));

    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[SHELL] Interactive shell ready\n");
    terminal_writestring("[SHELL] Type 'help' for available commands\n\n");
}

void shell_run(void)
{
    shell_display_prompt();

    while (1)
    {
        shell_process_input();
        __asm__ volatile("hlt");
    }
}

/* Shell task entry point - runs as a proper schedulable task */
void shell_task_entry(void)
{
    shell_display_prompt();

    while (1)
    {
        shell_process_input();
        /* Yield to other tasks instead of halting */
        task_yield();
    }
}