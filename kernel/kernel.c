/* kernel/kernel.c - Main kernel initialization
 *
 * FIXED: Proper filesystem priority: FAT → TarFS → RAMFS
 */

#include "kernel.h"
#include "fpu.h"
#include "task.h"
#include "scheduler.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../fs/tarfs.h"
#include "../fs/fat.h"

/* Linker symbols */
extern uint8_t kernel_start;
extern uint8_t kernel_end;

/* Helper functions */
static inline uint32_t align_down(uint32_t addr)
{
    return addr & ~(PAGE_SIZE - 1);
}

static inline uint32_t align_up(uint32_t addr)
{
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/* Kernel entry point */
void kernel_main(void)
{
    /* =========================================================
     * Step 1: Terminal (needed for debug output)
     * ========================================================= */
    terminal_initialize();

    terminal_writestring("╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║              OSComplex v0.1-alpha                        ║\n");
    terminal_writestring("║           An AI-Native Operating System                 ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
    terminal_writestring("[KERNEL] Booting OSComplex...\n");

    fpu_init();

    /* =========================================================
     * Step 2: Physical Memory Manager
     * ========================================================= */
    terminal_writestring("[PMM] Initializing physical memory manager...\n");

    /* Initialize PMM with conservative memory limit */
    pmm_init(MEMORY_LIMIT);

    /* Free usable memory above 1MB */
    pmm_init_region((void *)0x100000, MEMORY_LIMIT - 0x100000);

    /* Reserve kernel image */
    uint32_t kstart = align_down((uint32_t)&kernel_start);
    uint32_t kend = align_up((uint32_t)&kernel_end);
    uint32_t ksize = kend - kstart;

    pmm_deinit_region((void *)kstart, ksize);

    terminal_writestring("[PMM] Kernel memory reserved: 0x");
    char buf[16];
    utoa(kstart, buf, 16);
    terminal_writestring(buf);
    terminal_writestring(" - 0x");
    utoa(kend, buf, 16);
    terminal_writestring(buf);
    terminal_writestring("\n");
    terminal_writestring("[PMM] Physical memory manager ready\n\n");

    /* =========================================================
     * Step 3: Paging (basic identity mapping)
     * ========================================================= */
    terminal_writestring("[PAGING] Setting up initial paging...\n");
    paging_init();
    terminal_writestring("[PAGING] Paging enabled\n\n");

    /* =========================================================
     * Step 4: Virtual Memory Manager
     * CRITICAL: Must come BEFORE heap!
     * ========================================================= */
    terminal_writestring("[VMM] Initializing virtual memory manager...\n");
    vmm_init();
    terminal_writestring("[VMM] Virtual memory manager ready\n\n");

    /* =========================================================
     * Step 5: Kernel Heap
     * NOW we can use kmalloc!
     * ========================================================= */
    terminal_writestring("[HEAP] Initializing kernel heap...\n");
    heap_init();
    terminal_writestring("[HEAP] Kernel heap ready\n\n");

    /* =========================================================
     * Step 6: Interrupt Descriptor Table
     * ========================================================= */
    terminal_writestring("[IDT] Initializing interrupt table...\n");
    idt_init();
    terminal_writestring("[IDT] Interrupt table ready\n");

    /* =========================================================
     * Step 7: PIC initialization
     * ========================================================= */
    terminal_writestring("[PIC] Remapping interrupt controller...\n");
    pic_init();
    terminal_writestring("[PIC] Interrupt controller ready\n");

    /* =========================================================
     * Step 8: Device drivers
     * ========================================================= */
    terminal_writestring("[DRIVERS] Initializing device drivers...\n");
    keyboard_init();
    timer_init();
    ata_init();
    terminal_writestring("[DRIVERS] All drivers initialized\n");

    /* =========================================================
     * Step 9: Enable interrupts
     * ========================================================= */
    terminal_writestring("[KERNEL] Enabling interrupts...\n");
    __asm__ volatile("sti");
    terminal_writestring("[KERNEL] Interrupts enabled - system ready!\n\n");

    /* =========================================================
    * Step 9.5: User Mode Support
    * MUST come before task init!
    * ========================================================= */
    terminal_writestring("[KERNEL] Setting up user mode support...\n");
    gdt_init();
    tss_init();
    terminal_writestring("[KERNEL] User mode ready\n\n");

    /* =========================================================
     * Step 10: Task Management & Scheduler
     * MUST come after heap is initialized!
     * ========================================================= */
    terminal_writestring("[KERNEL] Initializing multitasking...\n");
    task_init();
    scheduler_init();
    syscall_init();
    terminal_writestring("[KERNEL] Multitasking ready\n\n");

    /* =========================================================
     * Step 11: Virtual File System
     * ========================================================= */
    terminal_writestring("[VFS] Initializing virtual file system...\n");
    vfs_init();
    terminal_writestring("[VFS] Virtual file system ready\n\n");

    /* Initialize filesystem drivers */
    ramfs_init();
    fat_init();
    tarfs_init();

    /* =========================================================
     * Step 12: Try to load persistent filesystem from disk
     * Priority: FAT16 → TarFS → RAMFS fallback
     * ========================================================= */
    terminal_writestring("[KERNEL] Loading root filesystem from disk...\n");
    
    bool filesystem_mounted = false;
    
    /* Try FAT16 first */
    terminal_writestring("[KERNEL] Attempting to mount FAT16...\n");
    vfs_node_t *fat_root = fat_mount(ATA_PRIMARY_MASTER, 0);
    
    if (fat_root) {
        vfs_root = fat_root;
        vfs_cwd = fat_root;
        filesystem_mounted = true;
        
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_writestring("[KERNEL] ✓ FAT16 filesystem mounted!\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    }
    
    /* If FAT failed, try TarFS */
    if (!filesystem_mounted) {
        terminal_writestring("[KERNEL] FAT16 not found, trying TarFS...\n");
        vfs_node_t *tar_root = tarfs_load(ATA_PRIMARY_MASTER, 0);
        
        if (tar_root) {
            vfs_root = tar_root;
            vfs_cwd = tar_root;
            filesystem_mounted = true;
            
            terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            terminal_writestring("[KERNEL] ✓ TarFS filesystem mounted!\n");
            terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
            
            /* Display boot message if it exists */
            int fd = vfs_open("/boot.txt", O_RDONLY);
            if (fd >= 0) {
                char buffer[512];
                int bytes = vfs_read(fd, buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    terminal_writestring("\n");
                    terminal_writestring(buffer);
                    terminal_writestring("\n");
                }
                vfs_close(fd);
            }
        }
    }
    
    /* If both failed, fall back to RAMFS */
    if (!filesystem_mounted) {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_writestring("[KERNEL] No persistent filesystem found\n");
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_writestring("[KERNEL] Using RAMFS (temporary storage)\n");
        /* RAMFS already set vfs_root and vfs_cwd in ramfs_init() */
    }

    terminal_writestring("[VFS] Root filesystem mounted\n\n");

    /* =========================================================
     * Step 13: AI subsystem
     * ========================================================= */
    terminal_writestring("[AI] Initializing AI learning system...\n");
    ai_init();
    terminal_writestring("[AI] AI system ready\n\n");

    /* =========================================================
     * Step 14: Shell
     * ========================================================= */
    terminal_writestring("[SHELL] Starting interactive shell...\n");
    shell_init();

    terminal_writestring("\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("╔══════════════════════════════════════════════════════════╗\n");
    terminal_writestring("║            System initialization complete!               ║\n");
    terminal_writestring("║                 Type 'help' to begin                     ║\n");
    terminal_writestring("╚══════════════════════════════════════════════════════════╝\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    /* =========================================================
     * Step 15: Run shell (never returns)
     * ========================================================= */
    shell_run();

    /* Should never reach here */
    while (1)
    {
        __asm__ volatile("cli; hlt");
    }
}