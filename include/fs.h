
#ifndef FS_H
#define FS_H
#define MAX_NAME_LEN 252
#define DIRENTS_PER_BLOCK (BLOCK_SIZE / sizeof(struct directory_entry))
#define MAX_POINTERS (BLOCK_SIZE / sizeof(uint32_t))

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "disk.h"

#define BLOCK_SIZE 4096
#define INODE_DIRECT_POINTERS 12

struct directory_entry
{
    uint32_t inode;
    char name[MAX_NAME_LEN];
} __attribute__((packed));

struct superblock
{
    uint32_t s_blocks_count;
    uint32_t s_inodes_count;
    uint32_t s_block_bitmap;
    uint32_t s_inode_bitmap;
    uint32_t s_inode_table_block_start;
    uint32_t s_data_blocks_start;
};

#define INODE_DIRECT_POINTERS 13

struct inode
{
    uint32_t i_size;
    uint32_t i_direct_pointers[INODE_DIRECT_POINTERS];
    uint32_t i_indirect_pointer;
    uint8_t i_is_directory;
    uint8_t padding[3];
};

union block
{
    struct superblock superblock;
    struct inode inodes[BLOCK_SIZE / sizeof(struct inode)];
    uint32_t bitmap[BLOCK_SIZE / sizeof(uint32_t)];
    uint8_t data[BLOCK_SIZE];
    struct directory_entry directory_entries[DIRENTS_PER_BLOCK];
    uint32_t pointers[MAX_POINTERS];
};

int fs_format();
int fs_mount();
void fs_unmount();
int fs_create(const char *path, int is_directory);
int fs_list(const char *path);
int fs_remove(const char *path);
int fs_write(const char *path, const void *buf, size_t count, int append);
int fs_read(const char *path, void *buf, size_t count, off_t offset);
void fs_stat();

#endif
