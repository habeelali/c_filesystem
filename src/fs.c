
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "fs.h"
#include "disk.h"

static int MOUNT_FLAG = 0;
static union block SUPERBLOCK;
static union block BLOCK_BITMAP;
static int DISK_OPEN_FLAG = 0;
static union block INODE_BITMAP;
static struct inode *INODE_TABLE;
#define ROOT_DIR_INODE 0

#define BITMAP_SET(bitmap, index) (bitmap[(index) / 32] |= (1 << ((index) % 32)))
#define BITMAP_CLEAR(bitmap, index) (bitmap[(index) / 32] &= ~(1 << ((index) % 32)))
#define BITMAP_TEST(bitmap, index) (bitmap[(index) / 32] & (1 << ((index) % 32)))

int fs_format()
{
    if (MOUNT_FLAG)
    {
        printf("Error: Disk is mounted. Unmount before formatting.\n");
        return -1;
    }

    uint32_t total_blocks = disk_size();
    if (total_blocks < 8)
    {
        printf("Error: Disk size too small for formatting.\n");
        return -1;
    }

    SUPERBLOCK.superblock.s_inodes_count = total_blocks;

#define BLOCK_SIZE 4096

#define INODE_SIZE sizeof(struct inode)

    uint32_t inodes_per_block = BLOCK_SIZE / INODE_SIZE;

    uint32_t inode_blocks = (SUPERBLOCK.superblock.s_inodes_count + inodes_per_block - 1) / inodes_per_block;

    SUPERBLOCK.superblock.s_blocks_count = total_blocks;
    SUPERBLOCK.superblock.s_block_bitmap = 1;
    SUPERBLOCK.superblock.s_inode_bitmap = 2;
    SUPERBLOCK.superblock.s_inode_table_block_start = 3;
    SUPERBLOCK.superblock.s_data_blocks_start = 3 + inode_blocks;

    if (SUPERBLOCK.superblock.s_data_blocks_start >= total_blocks)
    {
        printf("Error: Not enough space for data blocks.\n");
        return -1;
    }

    memset(&BLOCK_BITMAP, 0, sizeof(BLOCK_BITMAP));
    memset(&INODE_BITMAP, 0, sizeof(INODE_BITMAP));

    for (uint32_t i = 0; i < SUPERBLOCK.superblock.s_data_blocks_start; i++)
    {
        BITMAP_SET(BLOCK_BITMAP.bitmap, i);
    }

    BITMAP_SET(INODE_BITMAP.bitmap, ROOT_DIR_INODE);

    struct inode root_inode = {0};
    root_inode.i_size = BLOCK_SIZE;
    root_inode.i_is_directory = 1;
    root_inode.i_direct_pointers[0] = SUPERBLOCK.superblock.s_data_blocks_start;

    union block root_dir_block = {0};
    struct directory_entry *dir_entries = root_dir_block.directory_entries;

    dir_entries[0].inode = ROOT_DIR_INODE;
    strncpy(dir_entries[0].name, ".", MAX_NAME_LEN);
    dir_entries[0].name[MAX_NAME_LEN - 1] = '\0';

    dir_entries[1].inode = ROOT_DIR_INODE;
    strncpy(dir_entries[1].name, "..", MAX_NAME_LEN);
    dir_entries[1].name[MAX_NAME_LEN - 1] = '\0';

    if (disk_write(SUPERBLOCK.superblock.s_data_blocks_start, &root_dir_block) < 0)
    {
        printf("Error: Failed to write root directory data block.\n");
        return -1;
    }

    BITMAP_SET(BLOCK_BITMAP.bitmap, SUPERBLOCK.superblock.s_data_blocks_start);

    uint32_t inode_table_blocks = inode_blocks;
    uint32_t inode_index = 0;

    for (uint32_t i = 0; i < inode_table_blocks; i++)
    {
        union block inode_block = {0};

        for (uint32_t j = 0; j < inodes_per_block; j++)
        {
            if (inode_index >= SUPERBLOCK.superblock.s_inodes_count)
                break;

            if (inode_index == ROOT_DIR_INODE)
            {
                inode_block.inodes[j] = root_inode;
            }
            else
            {
                inode_block.inodes[j] = (struct inode){0};
            }
            inode_index++;
        }

        if (disk_write(SUPERBLOCK.superblock.s_inode_table_block_start + i, &inode_block) < 0)
        {
            printf("Error: Failed to write inode block %u.\n", i);
            return -1;
        }
    }

    if (disk_write(0, &SUPERBLOCK) < 0 ||
        disk_write(1, &BLOCK_BITMAP) < 0 ||
        disk_write(2, &INODE_BITMAP) < 0)
    {
        printf("Error: Failed to write filesystem metadata.\n");
        return -1;
    }

    printf("Filesystem formatted successfully.\n");
    return 0;
}

uint32_t allocate_data_block()
{
    uint32_t total_blocks = SUPERBLOCK.superblock.s_blocks_count;
    for (uint32_t i = SUPERBLOCK.superblock.s_data_blocks_start; i < total_blocks; i++)
    {
        if (!BITMAP_TEST(BLOCK_BITMAP.bitmap, i))
        {
            BITMAP_SET(BLOCK_BITMAP.bitmap, i);
            return i;
        }
    }
    return (uint32_t)-1;
}

int fs_mount()
{
    if (MOUNT_FLAG)
    {
        printf("Error: Filesystem already mounted.\n");
        return -1;
    }

    if (disk_read(0, &SUPERBLOCK) < 0 ||
        disk_read(1, &BLOCK_BITMAP) < 0 ||
        disk_read(2, &INODE_BITMAP) < 0)
    {
        printf("Error: Failed to read filesystem metadata.\n");
        return -1;
    }

    uint32_t inode_table_blocks = SUPERBLOCK.superblock.s_data_blocks_start -
                                  SUPERBLOCK.superblock.s_inode_table_block_start;
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
    uint32_t total_inodes = inodes_per_block * inode_table_blocks;

    INODE_TABLE = malloc(total_inodes * sizeof(struct inode));
    if (!INODE_TABLE)
    {
        printf("Error: Failed to allocate memory for inode table.\n");
        return -1;
    }

    uint32_t inode_index = 0;
    union block inode_block;

    for (uint32_t i = 0; i < inode_table_blocks; i++)
    {
        if (disk_read(SUPERBLOCK.superblock.s_inode_table_block_start + i, &inode_block) < 0)
        {
            printf("Error: Failed to load inode table from disk.\n");
            free(INODE_TABLE);
            return -1;
        }

        for (uint32_t j = 0; j < inodes_per_block && inode_index < total_inodes; j++, inode_index++)
        {
            INODE_TABLE[inode_index] = inode_block.inodes[j];
        }
    }

    MOUNT_FLAG = 1;
    DISK_OPEN_FLAG = 1;
    printf("Filesystem mounted successfully.\n");
    return 0;
}

void fs_unmount()
{
    if (!MOUNT_FLAG)
    {
        printf("Error: Filesystem not mounted.\n");
        return;
    }

    uint32_t inode_table_blocks = SUPERBLOCK.superblock.s_data_blocks_start -
                                  SUPERBLOCK.superblock.s_inode_table_block_start;
    uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
    uint32_t total_inodes = inodes_per_block * inode_table_blocks;
    uint32_t inode_index = 0;
    union block inode_block;

    for (uint32_t i = 0; i < inode_table_blocks; i++)
    {

        for (uint32_t j = 0; j < inodes_per_block && inode_index < total_inodes; j++, inode_index++)
        {
            inode_block.inodes[j] = INODE_TABLE[inode_index];
        }

        if (disk_write(SUPERBLOCK.superblock.s_inode_table_block_start + i, &inode_block) < 0)
        {
            printf("Error: Failed to write inode table to disk.\n");
        }
    }

    free(INODE_TABLE);
    MOUNT_FLAG = 0;
    printf("Filesystem unmounted successfully.\n");
}

int fs_create(const char *path, int is_directory)
{
    if (!MOUNT_FLAG)
    {
        printf("Error: Filesystem not mounted.\n");
        return -1;
    }

    if (!path || path[0] != '/')
    {
        printf("Error: Path must be absolute.\n");
        return -1;
    }

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token;
    char *rest = path_copy;
    uint32_t parent_inode_index = ROOT_DIR_INODE;
    uint32_t inode_index;
    char name[MAX_NAME_LEN];

    while ((token = strtok_r(rest, "/", &rest)))
    {
        strncpy(name, token, MAX_NAME_LEN);
        name[MAX_NAME_LEN - 1] = '\0';

        int is_last_component = (rest == NULL || *rest == '\0');
        int found = 0;
        struct inode *parent_inode = &INODE_TABLE[parent_inode_index];

        if (!parent_inode->i_is_directory)
        {
            printf("Error: Parent is not a directory.\n");
            return -1;
        }

        union block data_block;
        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (parent_inode->i_direct_pointers[dp] == 0)
                continue;

            if (disk_read(parent_inode->i_direct_pointers[dp], &data_block) < 0)
            {
                printf("Error: Failed to read directory data.\n");
                return -1;
            }

            struct directory_entry *entries = data_block.directory_entries;
            for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
            {
                entries[i].name[MAX_NAME_LEN - 1] = '\0';
                if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                {
                    inode_index = entries[i].inode;
                    found = 1;
                    break;
                }
            }
            if (found)
                break;
        }

        if (!found)
        {
            if (is_last_component || is_directory)
            {
                uint32_t total_inodes = SUPERBLOCK.superblock.s_inodes_count;
                uint32_t new_inode_index = (uint32_t)-1;
                for (uint32_t i = 0; i < total_inodes; i++)
                {
                    if (!BITMAP_TEST(INODE_BITMAP.bitmap, i))
                    {
                        new_inode_index = i;
                        break;
                    }
                }
                if (new_inode_index == (uint32_t)-1)
                {
                    printf("Error: No available inodes.\n");
                    return -1;
                }

                BITMAP_SET(INODE_BITMAP.bitmap, new_inode_index);
                struct inode *new_inode = &INODE_TABLE[new_inode_index];
                memset(new_inode, 0, sizeof(struct inode));
                new_inode->i_is_directory = is_last_component ? is_directory : 1;

                if (new_inode->i_is_directory)
                {
                    uint32_t total_blocks = SUPERBLOCK.superblock.s_blocks_count;
                    uint32_t data_block_index = (uint32_t)-1;
                    for (uint32_t i = SUPERBLOCK.superblock.s_data_blocks_start; i < total_blocks; i++)
                    {
                        if (!BITMAP_TEST(BLOCK_BITMAP.bitmap, i))
                        {
                            data_block_index = i;
                            break;
                        }
                    }
                    if (data_block_index == (uint32_t)-1)
                    {
                        printf("Error: No available data blocks.\n");
                        return -1;
                    }

                    BITMAP_SET(BLOCK_BITMAP.bitmap, data_block_index);
                    union block new_data_block = {0};
                    struct directory_entry *dir_entries = new_data_block.directory_entries;

                    dir_entries[0].inode = new_inode_index;
                    strncpy(dir_entries[0].name, ".", MAX_NAME_LEN);
                    dir_entries[0].name[MAX_NAME_LEN - 1] = '\0';

                    dir_entries[1].inode = parent_inode_index;
                    strncpy(dir_entries[1].name, "..", MAX_NAME_LEN);
                    dir_entries[1].name[MAX_NAME_LEN - 1] = '\0';

                    if (disk_write(data_block_index, &new_data_block) < 0)
                    {
                        printf("Error: Failed to write data block.\n");
                        return -1;
                    }

                    new_inode->i_direct_pointers[0] = data_block_index;
                    new_inode->i_size = BLOCK_SIZE;
                }
                else
                {
                    new_inode->i_size = 0;
                }

                int entry_added = 0;
                for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
                {
                    if (parent_inode->i_direct_pointers[dp] == 0)
                    {

                        uint32_t new_data_block_index = (uint32_t)-1;
                        for (uint32_t i = SUPERBLOCK.superblock.s_data_blocks_start; i < SUPERBLOCK.superblock.s_blocks_count; i++)
                        {
                            if (!BITMAP_TEST(BLOCK_BITMAP.bitmap, i))
                            {
                                new_data_block_index = i;
                                break;
                            }
                        }
                        if (new_data_block_index == (uint32_t)-1)
                        {
                            printf("Error: No available data blocks.\n");
                            return -1;
                        }

                        BITMAP_SET(BLOCK_BITMAP.bitmap, new_data_block_index);
                        union block new_data_block = {0};
                        parent_inode->i_direct_pointers[dp] = new_data_block_index;

                        parent_inode->i_size += BLOCK_SIZE;

                        struct directory_entry *entries = new_data_block.directory_entries;
                        entries[0].inode = new_inode_index;
                        strncpy(entries[0].name, name, MAX_NAME_LEN);
                        entries[0].name[MAX_NAME_LEN - 1] = '\0';
                        if (disk_write(new_data_block_index, &new_data_block) < 0)
                        {
                            printf("Error: Failed to write directory data.\n");
                            return -1;
                        }
                        entry_added = 1;
                        break;
                    }
                    else
                    {

                        if (disk_read(parent_inode->i_direct_pointers[dp], &data_block) < 0)
                        {
                            printf("Error: Failed to read directory data.\n");
                            return -1;
                        }

                        struct directory_entry *entries = data_block.directory_entries;
                        for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
                        {
                            if (entries[i].inode == 0)
                            {
                                entries[i].inode = new_inode_index;
                                strncpy(entries[i].name, name, MAX_NAME_LEN);
                                entries[i].name[MAX_NAME_LEN - 1] = '\0';
                                if (disk_write(parent_inode->i_direct_pointers[dp], &data_block) < 0)
                                {
                                    printf("Error: Failed to write directory data.\n");
                                    return -1;
                                }
                                entry_added = 1;
                                break;
                            }
                        }
                        if (entry_added)
                            break;
                    }
                }

                if (!entry_added)
                {
                    printf("Error: No space in directory.\n");
                    return -1;
                }

                parent_inode_index = new_inode_index;

                if (is_last_component)
                {
                    printf("Creating %s: %s\n", is_directory ? "directory" : "file", path);
                    return 0;
                }
            }
            else
            {
                printf("Creating intermediate directory: %s\n", name);
                char intermediate_path[256];
                snprintf(intermediate_path, sizeof(intermediate_path), "/%s", name);
                if (fs_create(intermediate_path, 1) == -1)
                {
                    printf("Error: Failed to create intermediate directory: %s\n", name);
                    return -1;
                }
            }
        }
        else
        {
            if (is_last_component)
            {
                printf("Error: File or directory already exists.\n");
                return -1;
            }
            parent_inode_index = inode_index;
        }
    }

    printf("Error: Cannot create root directory.\n");
    return -1;
}

int fs_remove(const char *path)
{
    if (!MOUNT_FLAG)
    {
        printf("Error: Filesystem not mounted.\n");
        return -1;
    }

    if (!path || path[0] != '/')
    {
        printf("Error: Path must be absolute and start with '/'.\n");
        return -1;
    }

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token;
    char *rest = path_copy;
    uint32_t parent_inode_index = ROOT_DIR_INODE;
    uint32_t target_inode_index = ROOT_DIR_INODE;
    char name[MAX_NAME_LEN];

    while ((token = strtok_r(rest, "/", &rest)))
    {
        strncpy(name, token, MAX_NAME_LEN);
        name[MAX_NAME_LEN - 1] = '\0';

        struct inode *parent_inode = &INODE_TABLE[parent_inode_index];
        if (!parent_inode->i_is_directory)
        {
            printf("Error: '%s' is not a directory.\n", path);
            return -1;
        }

        int found = 0;
        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (parent_inode->i_direct_pointers[dp] == 0)
            {
                continue;
            }

            union block parent_data_block;
            if (disk_read(parent_inode->i_direct_pointers[dp], &parent_data_block) < 0)
            {
                printf("Error: Failed to read directory data.\n");
                return -1;
            }

            struct directory_entry *entries = parent_data_block.directory_entries;
            for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
            {
                entries[i].name[MAX_NAME_LEN - 1] = '\0';
                if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                {
                    target_inode_index = entries[i].inode;
                    found = 1;
                    break;
                }
            }

            if (found)
            {
                break;
            }
        }

        if (!found)
        {
            printf("Error: '%s' not found.\n", path);
            return -1;
        }

        if (rest == NULL || *rest == '\0')
        {

            break;
        }
        else
        {

            parent_inode_index = target_inode_index;
        }
    }

    struct inode *target_inode = &INODE_TABLE[target_inode_index];

    if (target_inode->i_is_directory)
    {

        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (target_inode->i_direct_pointers[dp] == 0)
            {
                continue;
            }

            union block dir_data_block;
            if (disk_read(target_inode->i_direct_pointers[dp], &dir_data_block) < 0)
            {
                printf("Error: Failed to read directory data.\n");
                return -1;
            }

            struct directory_entry *entries = dir_data_block.directory_entries;
            for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
            {
                if (entries[i].inode != 0)
                {

                    if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0)
                    {
                        continue;
                    }

                    char child_path[256];
                    snprintf(child_path, sizeof(child_path), "%s/%s", path, entries[i].name);

                    if (fs_remove(child_path) < 0)
                    {
                        printf("Error: Failed to remove '%s'.\n", child_path);
                        return -1;
                    }
                }
            }

            BITMAP_CLEAR(BLOCK_BITMAP.bitmap, target_inode->i_direct_pointers[dp]);
            target_inode->i_direct_pointers[dp] = 0;
        }
    }
    else
    {

        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (target_inode->i_direct_pointers[dp] == 0)
            {
                continue;
            }

            BITMAP_CLEAR(BLOCK_BITMAP.bitmap, target_inode->i_direct_pointers[dp]);
            target_inode->i_direct_pointers[dp] = 0;
        }
    }

    BITMAP_CLEAR(INODE_BITMAP.bitmap, target_inode_index);
    memset(target_inode, 0, sizeof(struct inode));

    struct inode *parent_inode = &INODE_TABLE[parent_inode_index];
    for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
    {
        if (parent_inode->i_direct_pointers[dp] == 0)
        {
            continue;
        }

        union block parent_data_block;
        if (disk_read(parent_inode->i_direct_pointers[dp], &parent_data_block) < 0)
        {
            printf("Error: Failed to read parent directory data.\n");
            return -1;
        }

        struct directory_entry *entries = parent_data_block.directory_entries;
        int entry_found = 0;
        for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
        {
            if (entries[i].inode == target_inode_index)
            {
                memset(&entries[i], 0, sizeof(struct directory_entry));
                entry_found = 1;
                if (disk_write(parent_inode->i_direct_pointers[dp], &parent_data_block) < 0)
                {
                    printf("Error: Failed to update parent directory.\n");
                    return -1;
                }

                parent_inode->i_size -= sizeof(struct directory_entry);
                printf("Removed: %s\n", path);
                return 0;
            }
        }
    }

    printf("Error: Could not remove '%s'.\n", path);
    return -1;
}

int calculate_directory_size(uint32_t inode_index)
{
    struct inode *dir_inode = &INODE_TABLE[inode_index];

    if (!dir_inode->i_is_directory)
    {
        return dir_inode->i_size;
    }

    int total_size = 0;

    for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
    {
        if (dir_inode->i_direct_pointers[dp] == 0)
        {
            continue;
        }

        union block dir_data_block;
        if (disk_read(dir_inode->i_direct_pointers[dp], &dir_data_block) < 0)
        {
            printf("Error: Failed to read directory data.\n");
            return -1;
        }

        struct directory_entry *dir_entries = dir_data_block.directory_entries;

        for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
        {
            if (dir_entries[i].inode != 0)
            {

                if (strcmp(dir_entries[i].name, ".") == 0 || strcmp(dir_entries[i].name, "..") == 0)
                {
                    continue;
                }

                total_size += calculate_directory_size(dir_entries[i].inode);
            }
        }
    }

    dir_inode->i_size = total_size + BLOCK_SIZE;
    return dir_inode->i_size;
}

int fs_list(const char *path)
{
    if (!MOUNT_FLAG)
    {
        return -1;
    }

    if (!path || path[0] != '/')
    {
        return -1;
    }

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token;
    char *rest = path_copy;
    uint32_t current_inode_index = ROOT_DIR_INODE;
    uint32_t inode_index;
    char name[MAX_NAME_LEN];

    while ((token = strtok_r(rest, "/", &rest)))
    {
        strncpy(name, token, MAX_NAME_LEN);
        name[MAX_NAME_LEN - 1] = '\0';

        struct inode *current_inode = &INODE_TABLE[current_inode_index];

        if (!current_inode->i_is_directory)
        {
            return -1;
        }

        int found = 0;
        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (current_inode->i_direct_pointers[dp] == 0)
            {
                continue;
            }

            union block data_block;
            if (disk_read(current_inode->i_direct_pointers[dp], &data_block) < 0)
            {
                return -1;
            }

            struct directory_entry *entries = data_block.directory_entries;
            for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
            {
                entries[i].name[MAX_NAME_LEN - 1] = '\0';
                if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                {
                    inode_index = entries[i].inode;
                    found = 1;
                    break;
                }
            }

            if (found)
            {
                break;
            }
        }

        if (!found)
        {
            return -1;
        }

        current_inode_index = inode_index;
    }

    struct inode *dir_inode = &INODE_TABLE[current_inode_index];

    if (!dir_inode->i_is_directory)
    {
        printf("Error: '%s' is not a directory.\n", path);
        return -1;
    }

    calculate_directory_size(current_inode_index);

    for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
    {
        if (dir_inode->i_direct_pointers[dp] == 0)
        {
            continue;
        }

        union block dir_data_block;
        if (disk_read(dir_inode->i_direct_pointers[dp], &dir_data_block) < 0)
        {
            printf("Error: Failed to read directory data.\n");
            return -1;
        }

        struct directory_entry *dir_entries = dir_data_block.directory_entries;

        for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
        {
            if (dir_entries[i].inode != 0)
            {
                dir_entries[i].name[MAX_NAME_LEN - 1] = '\0';

                if (strcmp(dir_entries[i].name, ".") == 0 || strcmp(dir_entries[i].name, "..") == 0)
                {
                    continue;
                }

                struct inode *entry_inode = &INODE_TABLE[dir_entries[i].inode];
                printf("%s %llu\n", dir_entries[i].name, (unsigned long long)entry_inode->i_size);
            }
        }
    }

    return 0;
}

int fs_write(const char *path, const void *buf, size_t count, int append)
{
    if (!MOUNT_FLAG)
    {
        printf("Error: Filesystem not mounted.\n");
        return -1;
    }

    if (!path || path[0] != '/')
    {
        printf("Error: Path must be absolute and start with '/'.\n");
        return -1;
    }

    if (!buf || count == 0)
    {
        printf("Error: Invalid buffer or count.\n");
        return -1;
    }

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token;
    char *rest = NULL;
    uint32_t parent_inode_index = ROOT_DIR_INODE;
    uint32_t file_inode_index = 0;
    char name[MAX_NAME_LEN];

    struct inode *parent_inode = NULL;
    struct inode *file_inode = NULL;

    char current_path[256] = "/";

    while ((token = strtok_r(rest ? NULL : path_copy, "/", &rest)))
    {
        strncpy(name, token, MAX_NAME_LEN);
        name[MAX_NAME_LEN - 1] = '\0';

        if (strcmp(current_path, "/") == 0)
            snprintf(current_path + strlen(current_path), sizeof(current_path) - strlen(current_path), "%s", name);
        else
            snprintf(current_path + strlen(current_path), sizeof(current_path) - strlen(current_path), "/%s", name);

        parent_inode = &INODE_TABLE[parent_inode_index];

        if (!parent_inode->i_is_directory)
        {
            printf("Error: '%s' is not a directory.\n", name);
            return -1;
        }

        int found = 0;
        uint32_t inode_index = 0;

        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (parent_inode->i_direct_pointers[dp] == 0)
                continue;

            union block data_block;
            if (disk_read(parent_inode->i_direct_pointers[dp], &data_block) < 0)
            {
                printf("Error: Failed to read directory data.\n");
                return -1;
            }

            struct directory_entry *entries = data_block.directory_entries;
            for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
            {
                entries[i].name[MAX_NAME_LEN - 1] = '\0';
                if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                {
                    inode_index = entries[i].inode;
                    found = 1;
                    break;
                }
            }

            if (found)
                break;
        }

        if (rest == NULL || *rest == '\0')
        {

            if (found)
            {
                file_inode_index = inode_index;
                file_inode = &INODE_TABLE[file_inode_index];
                if (file_inode->i_is_directory)
                {
                    printf("Error: '%s' is a directory.\n", path);
                    return -1;
                }
            }
            else
            {

                if (fs_create(current_path, 0) == -1)
                {
                    printf("Error: Could not create file: %s\n", current_path);
                    return -1;
                }

                parent_inode = &INODE_TABLE[parent_inode_index];

                found = 0;
                for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
                {
                    if (parent_inode->i_direct_pointers[dp] == 0)
                        continue;

                    union block data_block;
                    if (disk_read(parent_inode->i_direct_pointers[dp], &data_block) < 0)
                    {
                        printf("Error: Failed to read directory data.\n");
                        return -1;
                    }

                    struct directory_entry *entries = data_block.directory_entries;
                    for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
                    {
                        entries[i].name[MAX_NAME_LEN - 1] = '\0';
                        if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                        {
                            file_inode_index = entries[i].inode;
                            file_inode = &INODE_TABLE[file_inode_index];
                            found = 1;
                            break;
                        }
                    }

                    if (found)
                        break;
                }

                if (!found)
                {
                    printf("Error: Failed to locate newly created file.\n");
                    return -1;
                }
            }
        }
        else
        {

            if (!found)
            {

                if (fs_create(current_path, 1) == -1)
                {
                    printf("Error: Could not create directory: '%s'.\n", current_path);
                    return -1;
                }

                parent_inode = &INODE_TABLE[parent_inode_index];

                found = 0;
                for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
                {
                    if (parent_inode->i_direct_pointers[dp] == 0)
                        continue;

                    union block data_block;
                    if (disk_read(parent_inode->i_direct_pointers[dp], &data_block) < 0)
                    {
                        printf("Error: Failed to read directory data.\n");
                        return -1;
                    }

                    struct directory_entry *entries = data_block.directory_entries;
                    for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
                    {
                        entries[i].name[MAX_NAME_LEN - 1] = '\0';
                        if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                        {
                            inode_index = entries[i].inode;
                            found = 1;
                            break;
                        }
                    }

                    if (found)
                        break;
                }

                if (!found)
                {
                    printf("Error: Failed to locate newly created directory.\n");
                    return -1;
                }
            }

            parent_inode_index = inode_index;
        }
    }

    if (file_inode == NULL)
    {
        printf("Error: File inode is NULL.\n");
        return -1;
    }

    off_t offset = append ? file_inode->i_size : 0;
    size_t remaining_bytes = count;
    const char *write_buf = (const char *)buf;

    while (remaining_bytes > 0)
    {
        size_t block_index = offset / BLOCK_SIZE;
        size_t block_offset = offset % BLOCK_SIZE;

        uint32_t data_block_num;

        if (block_index < INODE_DIRECT_POINTERS)
        {

            if (file_inode->i_direct_pointers[block_index] == 0)
            {
                uint32_t data_block_index = allocate_data_block();
                if (data_block_index == (uint32_t)-1)
                {
                    printf("Error: No available data blocks.\n");
                    return -1;
                }
                file_inode->i_direct_pointers[block_index] = data_block_index;
            }
            data_block_num = file_inode->i_direct_pointers[block_index];
        }
        else
        {

            block_index -= INODE_DIRECT_POINTERS;
            if (file_inode->i_indirect_pointer == 0)
            {
                uint32_t indirect_block_index = allocate_data_block();
                if (indirect_block_index == (uint32_t)-1)
                {
                    printf("Error: No available data blocks for indirect pointer.\n");
                    return -1;
                }
                file_inode->i_indirect_pointer = indirect_block_index;

                union block indirect_block = {0};
                if (disk_write(indirect_block_index, &indirect_block) < 0)
                {
                    printf("Error: Failed to write indirect block.\n");
                    return -1;
                }
            }

            union block indirect_block;
            if (disk_read(file_inode->i_indirect_pointer, &indirect_block) < 0)
            {
                printf("Error: Failed to read indirect block.\n");
                return -1;
            }

            if (block_index >= BLOCK_SIZE / sizeof(uint32_t))
            {
                printf("Error: File size exceeds maximum supported size.\n");
                return -1;
            }

            if (indirect_block.pointers[block_index] == 0)
            {
                uint32_t data_block_index = allocate_data_block();
                if (data_block_index == (uint32_t)-1)
                {
                    printf("Error: No available data blocks.\n");
                    return -1;
                }
                indirect_block.pointers[block_index] = data_block_index;

                if (disk_write(file_inode->i_indirect_pointer, &indirect_block) < 0)
                {
                    printf("Error: Failed to update indirect block.\n");
                    return -1;
                }
            }
            data_block_num = indirect_block.pointers[block_index];
        }

        union block data_block;
        if (disk_read(data_block_num, &data_block) < 0)
        {
            printf("Error: Failed to read data block.\n");
            return -1;
        }

        size_t writable_bytes = BLOCK_SIZE - block_offset;
        size_t bytes_to_write = (remaining_bytes < writable_bytes) ? remaining_bytes : writable_bytes;

        memcpy(data_block.data + block_offset, write_buf, bytes_to_write);

        if (disk_write(data_block_num, &data_block) < 0)
        {
            printf("Error: Failed to write data block.\n");
            return -1;
        }

        offset += bytes_to_write;
        write_buf += bytes_to_write;
        remaining_bytes -= bytes_to_write;
    }

    if ((size_t)offset > file_inode->i_size)
    {
        file_inode->i_size = offset;
    }

    printf("Successfully wrote %zu bytes to '%s'.\n", count, path);
    return 0;
}

int fs_read(const char *path, void *buf, size_t count, off_t offset)
{
    if (!MOUNT_FLAG)
    {
        printf("Error: Filesystem not mounted.\n");
        return -1;
    }

    if (!path || path[0] != '/')
    {
        printf("Error: Path must be absolute and start with '/'.\n");
        return -1;
    }

    if (!buf || count == 0)
    {
        printf("Error: Invalid buffer or count.\n");
        return -1;
    }

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token;
    char *rest = path_copy;
    uint32_t current_inode_index = ROOT_DIR_INODE;
    uint32_t file_inode_index = 0;
    char name[MAX_NAME_LEN];
    struct inode *file_inode = NULL;

    while ((token = strtok_r(rest, "/", &rest)))
    {
        strncpy(name, token, MAX_NAME_LEN);
        name[MAX_NAME_LEN - 1] = '\0';

        struct inode *current_inode = &INODE_TABLE[current_inode_index];

        if (!current_inode->i_is_directory)
        {
            printf("Error: '%s' is not a directory.\n", name);
            return -1;
        }

        int found = 0;
        for (int dp = 0; dp < INODE_DIRECT_POINTERS; dp++)
        {
            if (current_inode->i_direct_pointers[dp] == 0)
                continue;

            union block data_block;
            if (disk_read(current_inode->i_direct_pointers[dp], &data_block) < 0)
            {
                printf("Error: Failed to read directory data.\n");
                return -1;
            }

            struct directory_entry *entries = data_block.directory_entries;
            for (unsigned int i = 0; i < DIRENTS_PER_BLOCK; i++)
            {
                entries[i].name[MAX_NAME_LEN - 1] = '\0';
                if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0)
                {
                    file_inode_index = entries[i].inode;
                    file_inode = &INODE_TABLE[file_inode_index];
                    found = 1;
                    break;
                }
            }

            if (found)
                break;
        }

        if (!found)
        {
            printf("Error: '%s' not found.\n", path);
            return -1;
        }

        current_inode_index = file_inode_index;
    }

    if (file_inode == NULL)
    {
        printf("Error: File inode is NULL.\n");
        return -1;
    }

    if (file_inode->i_is_directory)
    {
        printf("Error: '%s' is a directory.\n", path);
        return -1;
    }

    if ((size_t)offset >= file_inode->i_size)
    {
        printf("Error: Offset is beyond the file size.\n");
        return 0;
    }

    if (offset + count > file_inode->i_size)
    {
        count = file_inode->i_size - offset;
    }

    size_t remaining_bytes = count;
    char *read_buf = (char *)buf;
    size_t total_read = 0;

    while (remaining_bytes > 0)
    {
        size_t block_index = offset / BLOCK_SIZE;
        size_t block_offset = offset % BLOCK_SIZE;

        uint32_t data_block_num;

        if (block_index < INODE_DIRECT_POINTERS)
        {
            if (file_inode->i_direct_pointers[block_index] == 0)
            {
                printf("Error: Invalid block access.\n");
                return -1;
            }
            data_block_num = file_inode->i_direct_pointers[block_index];
        }
        else
        {

            block_index -= INODE_DIRECT_POINTERS;

            if (file_inode->i_indirect_pointer == 0)
            {
                printf("Error: Invalid block access.\n");
                return -1;
            }

            union block indirect_block;
            if (disk_read(file_inode->i_indirect_pointer, &indirect_block) < 0)
            {
                printf("Error: Failed to read indirect block.\n");
                return -1;
            }

            if (block_index >= BLOCK_SIZE / sizeof(uint32_t))
            {
                printf("Error: Block index out of bounds.\n");
                return -1;
            }

            if (indirect_block.pointers[block_index] == 0)
            {
                printf("Error: Invalid block access.\n");
                return -1;
            }

            data_block_num = indirect_block.pointers[block_index];
        }

        union block data_block;
        if (disk_read(data_block_num, &data_block) < 0)
        {
            printf("Error: Failed to read data block.\n");
            return -1;
        }

        size_t readable_bytes = BLOCK_SIZE - block_offset;
        size_t bytes_to_read = (remaining_bytes < readable_bytes) ? remaining_bytes : readable_bytes;

        memcpy(read_buf, data_block.data + block_offset, bytes_to_read);

        read_buf += bytes_to_read;
        offset += bytes_to_read;
        remaining_bytes -= bytes_to_read;
        total_read += bytes_to_read;
    }

    printf("Successfully read %zu bytes from '%s'.\n", total_read, path);
    return total_read;
}

void fs_stat()
{
    if (!MOUNT_FLAG)
    {
        printf("Error: Filesystem not mounted.\n");
        return;
    }

    printf("Filesystem Statistics:\n");
    printf("Total Blocks: %u\n", SUPERBLOCK.superblock.s_blocks_count);
    printf("Total Inodes: %u\n", SUPERBLOCK.superblock.s_inodes_count);
}
