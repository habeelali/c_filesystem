#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"

static FILE *disk;                      // disk file pointer
static uint32_t number_of_blocks = 0;   // number of blocks in the disk
static int reads = 0;                   // number of reads from the disk
static int writes = 0;                  // number of writes to the disk

int disk_init(char *filename, int nblocks)
{
    // Open the file in write mode.
    disk = fopen(filename, "w+");

    // If the file could not be created, return -1.
    if (disk == NULL)
    {
        return -1;
    }

    // Create a block of zeros.
    char *block = calloc(BLOCK_SIZE, sizeof(char));

    // Write the blocks to the disk.
    for (int i = 0; i < nblocks; i++)
    {
        fwrite(block, BLOCK_SIZE, 1, disk);
    }

    // Free the block.
    free(block);

    // Set the number of blocks.
    number_of_blocks = nblocks;

    // Return 0.
    return 0;
}

int disk_size()
{
    // Return the number of blocks.
    return number_of_blocks;
}

/**
 * Checks if the given block number and buffer are valid.
 * 
 * @param blocknum The block number to be checked.
 * @param buf The buffer to be checked.
 * 
 * @return Returns 0 if both the block number and buffer are valid, otherwise returns a non-zero value.
 */
static int sanity_check(uint32_t blocknum, const void *buf)
{
    if (blocknum >= number_of_blocks)
    {
        printf("   > %d\n", blocknum);
        printf("   ERROR: Block number must be less than %d.\n", number_of_blocks);
        return -1;
    }

    if (buf == NULL)
    {
        printf("   ERROR: Buffer cannot be NULL.\n");
        return -1;
    }

    return 0;
}

int disk_read(uint32_t blocknum, void *buf)
{
    // Perform sanity check.
    if (sanity_check(blocknum, buf) != 0)
    {
        printf("   READ sanity check failed.\n");
        return -1;
    }

    // Seek to the block.
    fseek(disk, blocknum * BLOCK_SIZE, SEEK_SET);

    // Read the block.
    int blocks_read = fread(buf, BLOCK_SIZE, 1, disk);

    // If the number of blocks read is not 1, return -1.
    if (blocks_read != 1)
    {
        printf("   ERROR: Could not read block %d.\n", blocknum);
        return -1;
    }

    // Increment the number of reads.
    reads++;

    // Return the number of bytes read.
    return BLOCK_SIZE;
}

int disk_write(uint32_t blocknum, void *buf)
{
    // Perform sanity check.
    if (sanity_check(blocknum, buf) != 0)
    {
        printf("   WRITE sanity check failed.\n");
        return -1;
    }

    // Seek to the block.
    fseek(disk, blocknum * BLOCK_SIZE, SEEK_SET);

    // Write the block.
    int blocks_written = fwrite(buf, BLOCK_SIZE, 1, disk);

    // If the number of blocks written is not 1, return -1.
    if (blocks_written != 1)
    {
        printf("   ERROR: Could not write block %d.\n", blocknum);
        return -1;
    }

    // Increment the number of writes.
    writes++;

    // Return the number of bytes written.
    return BLOCK_SIZE;
}

/**
 * @param log: 0 if log is not required, 1 if log is required
 */
int disk_close(int log)
{
    // If the disk is not open, return -1.
    if (disk == NULL)
    {
        printf("   ERROR: Disk is not open.\n");
        return -1;
    }

    // If the disk could not be flushed, return -1.
    else if (fclose(disk) != 0)
    {
        printf("   ERROR: Could not close disk.\n");
        return -1;
    }

    // Print the number of reads and writes.
    if (log) 
    {
        printf("   Reads (Blocks): %d\n", reads);
        printf("   Writes (Blocks): %d\n", writes);
        printf("   Disk closed.\n");
    }

    // Clear the disk pointer.
    disk = NULL;

    // Return 0.
    return 0;
}