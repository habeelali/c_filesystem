/**
 * @file disk.h
 * @brief This header file contains the declarations of functions and variables related to disk operations.
 *
 * The functions declared in this file are used to initialize the disk, read from the disk, write to the disk, and close the disk.
 * The variables declared in this file are used to keep track of the number of blocks, reads, and writes.
 *
 */

#ifndef DISK_H
#define DISK_H

#include <stdio.h>
#include <stdint.h>

#define BLOCK_SIZE 4096 // 4 KB

/**
 * @brief Initializes a virtual disk with the given filename and number of blocks.
 *
 * @param filename The name of the file to use as the virtual disk.
 * @param nblocks The number of blocks to allocate for the virtual disk.
 * @return int Returns 0 on success, -1 on failure.
 */
int disk_init(char *filename, int nblocks);

/**
 * @brief Returns the size of the disk in number of blocks.
 *
 * @return int The size of the disk in number of blocks.
 */
int disk_size();

/**
 * @brief Reads data from the disk starting at the specified block number.
 *
 * @param blocknum The block number to start reading from.
 * @param buf A pointer to the buffer to read the data into.
 *
 * @return int The number of bytes read, or -1 if an error occurred.
 */
int disk_read(uint32_t blocknum, void *buf);

/**
 * @brief Writes data to the disk starting from the specified block number.
 *
 * @param blocknum The block number to start writing from.
 * @param buf A pointer to the buffer containing the data to write.
 * @return int The number of bytes written, or -1 if an error occurred.
 */
int disk_write(uint32_t blocknum, void *buf);

/**
 * @brief Closes the disk file and frees any allocated memory.
 *
 * @param log 1 if the disk operations should be logged, 0 otherwise.
 * @return int Returns 0 on success, -1 on failure.
 */
int disk_close(int log);

#endif
