/* fs/fat.c - FAT16 Filesystem Driver
 *
 * Complete FAT16 implementation with full read/write support.
 * Compatible with standard FAT16 tools (mkfs.fat, Windows, Linux).
 */

#include "fat.h"
#include "../kernel/kernel.h"
#include "../drivers/ata.h"

static fat_fs_t fat_fs;
static bool fat_initialized = false;

/* Forward declarations */
static int fat_node_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static int fat_node_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
static dirent_t *fat_node_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t *fat_node_finddir(vfs_node_t *node, const char *name);
static vfs_node_t *fat_node_create(vfs_node_t *parent, const char *name, uint32_t mode);

static vfs_node_t *fat_node_mkdir(vfs_node_t *parent, const char *name, uint32_t mode);
static int fat_node_unlink(vfs_node_t *parent, const char *name);
static int fat_node_rmdir(vfs_node_t *parent, const char *name);

static vfs_operations_t fat_ops = {
    .open = NULL, .close = NULL,
    .read = fat_node_read, .write = fat_node_write,
    .readdir = fat_node_readdir, .finddir = fat_node_finddir,
    .create = fat_node_create, .unlink = fat_node_unlink,
    .mkdir = fat_node_mkdir, .rmdir = fat_node_rmdir,
};

/* ================================================================
 * UTILITIES
 * ================================================================ */

void fat_filename_to_str(const char *fat_name, char *output) {
    int i, j = 0;
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) output[j++] = fat_name[i];
    if (fat_name[8] != ' ') {
        output[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) output[j++] = fat_name[i];
    }
    output[j] = '\0';
}

void fat_str_to_filename(const char *input, char *fat_name) {
    memset(fat_name, ' ', 11);
    const char *dot = strchr(input, '.');
    int name_len = dot ? (dot - input) : strlen(input);
    
    for (int i = 0; i < name_len && i < 8; i++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[i] = c;
    }
    
    if (dot) {
        const char *ext = dot + 1;
        for (int i = 0; i < 3 && ext[i]; i++) {
            char c = ext[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat_name[8 + i] = c;
        }
    }
}

bool fat_is_valid_entry(const fat_dir_entry_t *entry) {
    if (entry->name[0] == 0x00) return false;
    if ((uint8_t)entry->name[0] == 0xE5) return false;
    if ((entry->attributes & FAT_ATTR_LONG_NAME) == FAT_ATTR_LONG_NAME) return false;
    if (entry->attributes & FAT_ATTR_VOLUME_ID) return false;
    return true;
}

/* ================================================================
 * FAT TABLE
 * ================================================================ */

static uint16_t fat_get_next_cluster(uint16_t cluster) {
    if (cluster >= fat_fs.total_clusters + 2) return FAT_CLUSTER_EOC;
    return fat_fs.fat_table[cluster];
}

static void fat_set_cluster(uint16_t cluster, uint16_t value) {
    if (cluster >= fat_fs.total_clusters + 2) return;
    fat_fs.fat_table[cluster] = value;
    fat_fs.fat_dirty = true;
}

static uint16_t fat_alloc_cluster(void) {
    for (uint16_t i = 2; i < fat_fs.total_clusters + 2; i++) {
        if (fat_fs.fat_table[i] == FAT_CLUSTER_FREE) {
            fat_set_cluster(i, FAT_CLUSTER_EOC);
            return i;
        }
    }
    return 0;
}

static void fat_free_cluster_chain(uint16_t cluster) {
    while (cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
        uint16_t next = fat_get_next_cluster(cluster);
        fat_set_cluster(cluster, FAT_CLUSTER_FREE);
        cluster = next;
    }
}

/* ================================================================
 * DISK I/O
 * ================================================================ */

static uint32_t cluster_to_lba(uint16_t cluster) {
    if (cluster < 2) return 0;
    return fat_fs.data_start + ((cluster - 2) * fat_fs.sectors_per_cluster);
}

static int fat_read_cluster(uint16_t cluster, uint8_t *buffer) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint8_t i = 0; i < fat_fs.sectors_per_cluster; i++) {
        if (ata_read_sector(fat_fs.drive, lba + i, buffer + (i * 512)) < 0)
            return -1;
    }
    return 0;
}

static int fat_write_cluster(uint16_t cluster, const uint8_t *buffer) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint8_t i = 0; i < fat_fs.sectors_per_cluster; i++) {
        if (ata_write_sector(fat_fs.drive, lba + i, buffer + (i * 512)) < 0)
            return -1;
    }
    return 0;
}

/* ================================================================
 * DIRECTORY SEARCH & MANIPULATION
 * ================================================================ */

static fat_dir_entry_t *fat_find_in_dir(uint16_t dir_cluster, const char *name) {
    static fat_dir_entry_t result;
    static uint8_t buffer[2048];
    char fat_name[11];
    fat_str_to_filename(name, fat_name);
    
    /* Root directory (cluster 0) */
    if (dir_cluster == 0) {
        uint32_t root_sectors = (fat_fs.root_entries * 32 + 511) / 512;
        for (uint32_t sec = 0; sec < root_sectors; sec++) {
            if (ata_read_sector(fat_fs.drive, fat_fs.root_dir_start + sec, buffer) < 0)
                continue;
            
            fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
            for (uint32_t i = 0; i < 16; i++) {
                if (!fat_is_valid_entry(&entries[i])) continue;
                if (memcmp(entries[i].name, fat_name, 11) == 0) {
                    memcpy(&result, &entries[i], sizeof(fat_dir_entry_t));
                    return &result;
                }
            }
        }
        return NULL;
    }
    
    /* Regular directory */
    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
        if (fat_read_cluster(cluster, buffer) < 0) return NULL;
        
        fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
        uint32_t count = (fat_fs.sectors_per_cluster * 512) / 32;
        
        for (uint32_t i = 0; i < count; i++) {
            if (!fat_is_valid_entry(&entries[i])) continue;
            if (memcmp(entries[i].name, fat_name, 11) == 0) {
                memcpy(&result, &entries[i], sizeof(fat_dir_entry_t));
                return &result;
            }
        }
        cluster = fat_get_next_cluster(cluster);
    }
    return NULL;
}

/* Find a free directory entry and return its location */
static int fat_find_free_entry(uint16_t dir_cluster, uint32_t *sector_out, uint32_t *offset_out) {
    static uint8_t buffer[2048];
    
    /* Root directory */
    if (dir_cluster == 0) {
        uint32_t root_sectors = (fat_fs.root_entries * 32 + 511) / 512;
        for (uint32_t sec = 0; sec < root_sectors; sec++) {
            if (ata_read_sector(fat_fs.drive, fat_fs.root_dir_start + sec, buffer) < 0)
                continue;
            
            fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
            for (uint32_t i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                    *sector_out = fat_fs.root_dir_start + sec;
                    *offset_out = i * 32;
                    return 0;
                }
            }
        }
        return -1;  /* Root directory full */
    }
    
    /* Regular directory */
    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
        if (fat_read_cluster(cluster, buffer) < 0) return -1;
        
        fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
        uint32_t count = (fat_fs.sectors_per_cluster * 512) / 32;
        
        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                *sector_out = cluster_to_lba(cluster);
                *offset_out = i * 32;
                return 0;
            }
        }
        
        cluster = fat_get_next_cluster(cluster);
    }
    
    return -1;  /* No free entry found */
}

/* Write a directory entry to disk */
static int fat_write_dir_entry(uint32_t sector, uint32_t offset, fat_dir_entry_t *entry) {
    static uint8_t buffer[512];
    
    /* Read sector */
    if (ata_read_sector(fat_fs.drive, sector, buffer) < 0)
        return -1;
    
    /* Modify entry */
    memcpy(buffer + offset, entry, sizeof(fat_dir_entry_t));
    
    /* Write back */
    if (ata_write_sector(fat_fs.drive, sector, buffer) < 0)
        return -1;
    
    return 0;
}

/* ================================================================
 * VFS OPERATIONS
 * ================================================================ */

static int fat_node_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || node->type != VFS_FILE) return -1;
    fat_node_data_t *data = (fat_node_data_t *)node->impl_data;
    if (!data || offset >= node->size) return 0;

    if (offset + size > node->size) size = node->size - offset;

    uint32_t cluster_size = fat_fs.sectors_per_cluster * 512;
    uint16_t cluster = data->first_cluster;

    /* Skip to offset */
    uint32_t skip = offset / cluster_size;
    for (uint32_t i = 0; i < skip && cluster < FAT_CLUSTER_EOC; i++)
        cluster = fat_get_next_cluster(cluster);

    uint32_t cluster_offset = offset % cluster_size;
    uint32_t bytes_read = 0;
    static uint8_t buf[2048];

    int cluster_num = 0;
    while (bytes_read < size && cluster >= 2 && cluster < FAT_CLUSTER_EOC) {

        if (fat_read_cluster(cluster, buf) < 0) break;

        uint32_t to_read = cluster_size - cluster_offset;
        if (to_read > size - bytes_read) to_read = size - bytes_read;

        memcpy(buffer + bytes_read, buf + cluster_offset, to_read);
        bytes_read += to_read;
        cluster_offset = 0;
        cluster = fat_get_next_cluster(cluster);
        cluster_num++;
    }
    return bytes_read;
}


static int fat_node_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    if (!node || node->type != VFS_FILE) return -1;
    fat_node_data_t *data = (fat_node_data_t *)node->impl_data;
    if (!data) return -1;
    
    /* Allocate first cluster if empty */
    if (data->first_cluster == 0) {
        data->first_cluster = fat_alloc_cluster();
        if (data->first_cluster == 0) return -1;
    }
    
    uint32_t cluster_size = fat_fs.sectors_per_cluster * 512;
    uint16_t cluster = data->first_cluster;
    
    /* Extend chain as needed */
    uint32_t needed_clusters = (offset + size + cluster_size - 1) / cluster_size;
    uint32_t current_clusters = 1;
    
    while (current_clusters < needed_clusters) {
        uint16_t next = fat_get_next_cluster(cluster);
        if (next >= FAT_CLUSTER_EOC) {
            next = fat_alloc_cluster();
            if (next == 0) break;
            fat_set_cluster(cluster, next);
        }
        cluster = next;
        current_clusters++;
    }
    
    /* Write data */
    cluster = data->first_cluster;
    uint32_t skip = offset / cluster_size;
    for (uint32_t i = 0; i < skip && cluster < FAT_CLUSTER_EOC; i++)
        cluster = fat_get_next_cluster(cluster);
    
    uint32_t cluster_offset = offset % cluster_size;
    uint32_t bytes_written = 0;
    static uint8_t buf[2048];
    
    while (bytes_written < size && cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
        if (cluster_offset > 0 || size - bytes_written < cluster_size) {
            fat_read_cluster(cluster, buf);
        }
        
        uint32_t to_write = cluster_size - cluster_offset;
        if (to_write > size - bytes_written) to_write = size - bytes_written;
        
        memcpy(buf + cluster_offset, buffer + bytes_written, to_write);
        if (fat_write_cluster(cluster, buf) < 0) break;
        
        bytes_written += to_write;
        cluster_offset = 0;
        cluster = fat_get_next_cluster(cluster);
    }
    
    if (offset + bytes_written > node->size)
        node->size = offset + bytes_written;
    
    /* ================================================================
     * CRITICAL FIX: Update directory entry with new file size
     * ================================================================ */
    if (bytes_written > 0 && node->parent) {
        fat_node_data_t *parent_data = (fat_node_data_t *)node->parent->impl_data;
        uint16_t parent_cluster = parent_data ? parent_data->first_cluster : 0;
        
        /* Find and update the directory entry */
        static uint8_t dir_buffer[2048];
        char fat_name[11];
        fat_str_to_filename(node->name, fat_name);
        
        /* Root directory */
        if (parent_cluster == 0) {
            uint32_t root_sectors = (fat_fs.root_entries * 32 + 511) / 512;
            for (uint32_t sec = 0; sec < root_sectors; sec++) {
                if (ata_read_sector(fat_fs.drive, fat_fs.root_dir_start + sec, dir_buffer) < 0)
                    continue;
                
                fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_buffer;
                for (uint32_t i = 0; i < 16; i++) {
                    if (fat_is_valid_entry(&entries[i]) && 
                        memcmp(entries[i].name, fat_name, 11) == 0) {
                        /* Update file size and first cluster */
                        entries[i].file_size = node->size;
                        entries[i].first_cluster = data->first_cluster;
                        ata_write_sector(fat_fs.drive, fat_fs.root_dir_start + sec, dir_buffer);
                        goto size_updated;
                    }
                }
            }
        }
        /* Regular directory */
        else {
            uint16_t cluster = parent_cluster;
            while (cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
                if (fat_read_cluster(cluster, dir_buffer) < 0) break;
                
                fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_buffer;
                uint32_t count = (fat_fs.sectors_per_cluster * 512) / 32;
                
                for (uint32_t i = 0; i < count; i++) {
                    if (fat_is_valid_entry(&entries[i]) && 
                        memcmp(entries[i].name, fat_name, 11) == 0) {
                        /* Update file size and first cluster */
                        entries[i].file_size = node->size;
                        entries[i].first_cluster = data->first_cluster;
                        fat_write_cluster(cluster, dir_buffer);
                        goto size_updated;
                    }
                }
                cluster = fat_get_next_cluster(cluster);
            }
        }
    }
size_updated:
    
    /* Sync FAT table to disk */
    fat_sync();
    
    return bytes_written;
}

static dirent_t *fat_node_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || node->type != VFS_DIRECTORY) return NULL;
    
    static dirent_t dent;
    static uint8_t buffer[2048];
    fat_node_data_t *data = (fat_node_data_t *)node->impl_data;
    uint16_t cluster = data ? data->first_cluster : 0;
    uint32_t current = 0;
    
    /* Root directory */
    if (cluster == 0) {
        uint32_t root_sectors = (fat_fs.root_entries * 32 + 511) / 512;
        for (uint32_t sec = 0; sec < root_sectors; sec++) {
            if (ata_read_sector(fat_fs.drive, fat_fs.root_dir_start + sec, buffer) < 0)
                continue;
            
            fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
            for (uint32_t i = 0; i < 16; i++) {
                if (!fat_is_valid_entry(&entries[i])) continue;
                if (current == index) {
                    fat_filename_to_str(entries[i].name, dent.name);
                    dent.inode = entries[i].first_cluster;
                    dent.type = (entries[i].attributes & FAT_ATTR_DIRECTORY) ? 
                                VFS_DIRECTORY : VFS_FILE;
                    return &dent;
                }
                current++;
            }
        }
        return NULL;
    }
    
    /* Regular directory */
    while (cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
        if (fat_read_cluster(cluster, buffer) < 0) return NULL;
        
        fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
        uint32_t count = (fat_fs.sectors_per_cluster * 512) / 32;
        
        for (uint32_t i = 0; i < count; i++) {
            if (!fat_is_valid_entry(&entries[i])) continue;
            if (current == index) {
                fat_filename_to_str(entries[i].name, dent.name);
                dent.inode = entries[i].first_cluster;
                dent.type = (entries[i].attributes & FAT_ATTR_DIRECTORY) ? 
                            VFS_DIRECTORY : VFS_FILE;
                return &dent;
            }
            current++;
        }
        cluster = fat_get_next_cluster(cluster);
    }
    return NULL;
}

static vfs_node_t *fat_node_finddir(vfs_node_t *node, const char *name) {
    if (!node || node->type != VFS_DIRECTORY) return NULL;
    
    fat_node_data_t *data = (fat_node_data_t *)node->impl_data;
    uint16_t dir_cluster = data ? data->first_cluster : 0;
    
    fat_dir_entry_t *entry = fat_find_in_dir(dir_cluster, name);
    if (!entry) return NULL;
    
    vfs_node_t *child = kmalloc(sizeof(vfs_node_t));
    if (!child) return NULL;
    
    memset(child, 0, sizeof(vfs_node_t));
    fat_filename_to_str(entry->name, child->name);
    child->type = (entry->attributes & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    child->size = entry->file_size;
    child->ops = &fat_ops;
    child->parent = node;
    
    fat_node_data_t *child_data = kmalloc(sizeof(fat_node_data_t));
    if (!child_data) {
        kfree(child);
        return NULL;
    }
    child_data->first_cluster = entry->first_cluster;
    child->impl_data = child_data;
    
    return child;
}

static vfs_node_t *fat_node_create(vfs_node_t *parent, const char *name, uint32_t mode) {
    (void)mode;
    if (!parent || parent->type != VFS_DIRECTORY) return NULL;
    
    fat_node_data_t *parent_data = (fat_node_data_t *)parent->impl_data;
    uint16_t parent_cluster = parent_data ? parent_data->first_cluster : 0;
    
    /* Check if file already exists */
    if (fat_find_in_dir(parent_cluster, name)) {
        return NULL;  /* File exists */
    }
    
    /* Find free directory entry */
    uint32_t entry_sector, entry_offset;
    if (fat_find_free_entry(parent_cluster, &entry_sector, &entry_offset) < 0) {
        return NULL;  /* Directory full */
    }
    
    /* Allocate first cluster for the file */
    uint16_t first_cluster = fat_alloc_cluster();
    if (first_cluster == 0) {
        return NULL;  /* Disk full */
    }
    
    /* Create directory entry */
    fat_dir_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    
    fat_str_to_filename(name, entry.name);
    entry.attributes = 0;  /* Regular file */
    entry.first_cluster = first_cluster;
    entry.file_size = 0;
    
    /* Write directory entry */
    if (fat_write_dir_entry(entry_sector, entry_offset, &entry) < 0) {
        fat_free_cluster_chain(first_cluster);
        return NULL;
    }
    
    /* Sync FAT table */
    fat_sync();
    
    /* Create VFS node */
    vfs_node_t *node = kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, name, 255);
    node->type = VFS_FILE;
    node->size = 0;
    node->ops = &fat_ops;
    node->parent = parent;
    
    fat_node_data_t *node_data = kmalloc(sizeof(fat_node_data_t));
    if (!node_data) {
        kfree(node);
        return NULL;
    }
    node_data->first_cluster = first_cluster;
    node->impl_data = node_data;
    
    return node;
}

/* ================================================================
 * MOUNT
 * ================================================================ */

vfs_node_t *fat_mount(uint8_t drive, uint32_t partition_start) {
    terminal_writestring("[FAT] Mounting FAT16 filesystem...\n");
    
    fat_boot_sector_t *boot = kmalloc(512);
    if (!boot || ata_read_sector(drive, partition_start, (uint8_t *)boot) < 0) {
        terminal_writestring("[FAT] ERROR: Cannot read boot sector\n");
        if (boot) kfree(boot);
        return NULL;
    }
    
    if (boot->boot_signature_end != 0xAA55) {
        terminal_writestring("[FAT] ERROR: Invalid boot signature\n");
        kfree(boot);
        return NULL;
    }
    
    memset(&fat_fs, 0, sizeof(fat_fs));
    fat_fs.drive = drive;
    fat_fs.partition_start = partition_start;
    fat_fs.bytes_per_sector = boot->bytes_per_sector;
    fat_fs.sectors_per_cluster = boot->sectors_per_cluster;
    fat_fs.reserved_sectors = boot->reserved_sectors;
    fat_fs.num_fats = boot->num_fats;
    fat_fs.root_entries = boot->root_entries;
    fat_fs.sectors_per_fat = boot->sectors_per_fat;
    fat_fs.total_sectors = boot->total_sectors_16 ? boot->total_sectors_16 : boot->total_sectors_32;
    
    fat_fs.fat_start = partition_start + fat_fs.reserved_sectors;
    fat_fs.root_dir_start = fat_fs.fat_start + (fat_fs.num_fats * fat_fs.sectors_per_fat);
    uint32_t root_sectors = (fat_fs.root_entries * 32 + 511) / 512;
    fat_fs.data_start = fat_fs.root_dir_start + root_sectors;
    fat_fs.total_clusters = (fat_fs.total_sectors - (fat_fs.data_start - partition_start)) / fat_fs.sectors_per_cluster;
    
    kfree(boot);
    
    /* Load FAT table */
    uint32_t fat_bytes = fat_fs.sectors_per_fat * 512;
    fat_fs.fat_table = kmalloc(fat_bytes);
    if (!fat_fs.fat_table) {
        terminal_writestring("[FAT] ERROR: Out of memory\n");
        return NULL;
    }
    
    for (uint32_t i = 0; i < fat_fs.sectors_per_fat; i++) {
        if (ata_read_sector(drive, fat_fs.fat_start + i, 
                           (uint8_t *)fat_fs.fat_table + (i * 512)) < 0) {
            terminal_writestring("[FAT] ERROR: Cannot load FAT table\n");
            kfree(fat_fs.fat_table);
            return NULL;
        }
    }
    
    fat_fs.fat_dirty = false;
    fat_initialized = true;
    
    /* Create root node */
    vfs_node_t *root = kmalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(fat_fs.fat_table);
        return NULL;
    }
    
    memset(root, 0, sizeof(vfs_node_t));
    root->type = VFS_DIRECTORY;
    root->ops = &fat_ops;
    
    fat_node_data_t *root_data = kmalloc(sizeof(fat_node_data_t));
    if (!root_data) {
        kfree(root);
        kfree(fat_fs.fat_table);
        return NULL;
    }
    root_data->first_cluster = 0;
    root->impl_data = root_data;
    
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("[FAT] FAT16 mounted successfully\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    
    return root;
}

int fat_sync(void) {
    if (!fat_initialized || !fat_fs.fat_dirty) return 0;
    
    for (uint8_t i = 0; i < fat_fs.num_fats; i++) {
        uint32_t fat_lba = fat_fs.fat_start + (i * fat_fs.sectors_per_fat);
        for (uint32_t j = 0; j < fat_fs.sectors_per_fat; j++) {
            if (ata_write_sector(fat_fs.drive, fat_lba + j,
                                (uint8_t *)fat_fs.fat_table + (j * 512)) < 0)
                return -1;
        }
    }
    
    fat_fs.fat_dirty = false;
    return 0;
}

void fat_unmount(vfs_node_t *root) {
    (void)root;
    if (!fat_initialized) return;
    fat_sync();
    if (fat_fs.fat_table) kfree(fat_fs.fat_table);
    fat_initialized = false;
}

void fat_init(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("[FAT] FAT16 driver initialized\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

/* ================================================================
 * DIRECTORY CREATION
 * ================================================================ */

static vfs_node_t *fat_node_mkdir(vfs_node_t *parent, const char *name, uint32_t mode) {
    (void)mode;
    if (!parent || parent->type != VFS_DIRECTORY) return NULL;
    
    fat_node_data_t *parent_data = (fat_node_data_t *)parent->impl_data;
    uint16_t parent_cluster = parent_data ? parent_data->first_cluster : 0;
    
    /* Check if directory already exists */
    if (fat_find_in_dir(parent_cluster, name)) {
        return NULL;  /* Already exists */
    }
    
    /* Find free directory entry */
    uint32_t entry_sector, entry_offset;
    if (fat_find_free_entry(parent_cluster, &entry_sector, &entry_offset) < 0) {
        return NULL;  /* Parent directory full */
    }
    
    /* Allocate cluster for new directory */
    uint16_t dir_cluster = fat_alloc_cluster();
    if (dir_cluster == 0) {
        return NULL;  /* Disk full */
    }
    
    /* Initialize directory cluster with . and .. entries */
    static uint8_t cluster_buf[2048];
    memset(cluster_buf, 0, fat_fs.sectors_per_cluster * 512);
    
    fat_dir_entry_t *entries = (fat_dir_entry_t *)cluster_buf;
    
    /* . entry (self) */
    memset(&entries[0], 0, sizeof(fat_dir_entry_t));
    memcpy(entries[0].name, ".          ", 11);
    entries[0].attributes = FAT_ATTR_DIRECTORY;
    entries[0].first_cluster = dir_cluster;
    
    /* .. entry (parent) */
    memset(&entries[1], 0, sizeof(fat_dir_entry_t));
    memcpy(entries[1].name, "..         ", 11);
    entries[1].attributes = FAT_ATTR_DIRECTORY;
    entries[1].first_cluster = parent_cluster;
    
    /* Write directory cluster */
    if (fat_write_cluster(dir_cluster, cluster_buf) < 0) {
        fat_free_cluster_chain(dir_cluster);
        return NULL;
    }
    
    /* Create directory entry in parent */
    fat_dir_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    
    fat_str_to_filename(name, entry.name);
    entry.attributes = FAT_ATTR_DIRECTORY;
    entry.first_cluster = dir_cluster;
    entry.file_size = 0;
    
    /* Write parent directory entry */
    if (fat_write_dir_entry(entry_sector, entry_offset, &entry) < 0) {
        fat_free_cluster_chain(dir_cluster);
        return NULL;
    }
    
    /* Sync FAT table */
    fat_sync();
    
    /* Create VFS node */
    vfs_node_t *node = kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, name, 255);
    node->type = VFS_DIRECTORY;
    node->size = 0;
    node->ops = &fat_ops;
    node->parent = parent;
    
    fat_node_data_t *node_data = kmalloc(sizeof(fat_node_data_t));
    if (!node_data) {
        kfree(node);
        return NULL;
    }
    node_data->first_cluster = dir_cluster;
    node->impl_data = node_data;
    
    return node;
}

/* ================================================================
 * FILE/DIRECTORY DELETION
 * ================================================================ */

static int fat_node_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || parent->type != VFS_DIRECTORY) return -1;
    
    fat_node_data_t *parent_data = (fat_node_data_t *)parent->impl_data;
    uint16_t parent_cluster = parent_data ? parent_data->first_cluster : 0;
    
    /* Find the file */
    static uint8_t buffer[2048];
    char fat_name[11];
    fat_str_to_filename(name, fat_name);
    
    /* Search in root directory */
    if (parent_cluster == 0) {
        uint32_t root_sectors = (fat_fs.root_entries * 32 + 511) / 512;
        for (uint32_t sec = 0; sec < root_sectors; sec++) {
            if (ata_read_sector(fat_fs.drive, fat_fs.root_dir_start + sec, buffer) < 0)
                continue;
            
            fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
            for (uint32_t i = 0; i < 16; i++) {
                if (!fat_is_valid_entry(&entries[i])) continue;
                if (memcmp(entries[i].name, fat_name, 11) == 0) {
                    /* Found it - mark as deleted */
                    uint16_t first_cluster = entries[i].first_cluster;
                    entries[i].name[0] = 0xE5;  /* Deleted marker */
                    
                    /* Write back */
                    if (ata_write_sector(fat_fs.drive, fat_fs.root_dir_start + sec, buffer) < 0)
                        return -1;
                    
                    /* Free cluster chain */
                    fat_free_cluster_chain(first_cluster);
                    fat_sync();
                    return 0;
                }
            }
        }
        return -1;
    }
    
    /* Search in regular directory */
    uint16_t cluster = parent_cluster;
    while (cluster >= 2 && cluster < FAT_CLUSTER_EOC) {
        if (fat_read_cluster(cluster, buffer) < 0) return -1;
        
        fat_dir_entry_t *entries = (fat_dir_entry_t *)buffer;
        uint32_t count = (fat_fs.sectors_per_cluster * 512) / 32;
        
        for (uint32_t i = 0; i < count; i++) {
            if (!fat_is_valid_entry(&entries[i])) continue;
            if (memcmp(entries[i].name, fat_name, 11) == 0) {
                /* Found it */
                uint16_t first_cluster = entries[i].first_cluster;
                entries[i].name[0] = 0xE5;  /* Deleted marker */
                
                /* Write back */
                if (fat_write_cluster(cluster, buffer) < 0)
                    return -1;
                
                /* Free cluster chain */
                fat_free_cluster_chain(first_cluster);
                fat_sync();
                return 0;
            }
        }
        
        cluster = fat_get_next_cluster(cluster);
    }
    
    return -1;  /* Not found */
}

static int fat_node_rmdir(vfs_node_t *parent, const char *name) {
    /* For now, just call unlink - should check if directory is empty */
    return fat_node_unlink(parent, name);
}