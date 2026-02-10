//
// Created by stepan on 2/10/26.
//

#ifndef INC_10_EXT2_FUSE_BLKITER_H
#define INC_10_EXT2_FUSE_BLKITER_H

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <ext2fs/ext2_fs.h>
#include <errno.h>
#include <stdio.h>

struct ext2_fs
{
    int fd;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks_count;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t groups_count;
    uint32_t inodes_count;
    uint32_t bgdt_size;
    struct ext2_group_desc *bgdt;
};

struct ext2_blkiter
{
    int fd;
    uint32_t block_size;
    uint32_t layer[4][EXT2_MAX_BLOCK_SIZE];
    int l1, l2, l3, ind;
    uint64_t file_size;
};


int ext2_fs_init(struct ext2_fs **fs, int fd);
void ext2_fs_free(struct ext2_fs *fs);
int ext2_blkiter_init(struct ext2_blkiter **i, struct ext2_fs *fs, uint32_t ino);
int ext2_blkiter_next(struct ext2_blkiter *i, int *blkno);
void ext2_blkiter_free(struct ext2_blkiter *i);

int ext2_openat(uint32_t ino, const char *child, struct ext2_fs *fs, struct ext2_dir_entry_2 *result);
int ext2_readlink(uint32_t ino, struct ext2_fs *fs, char *result);
int ext2_readinode(const uint32_t ino, struct ext2_fs *fs, struct ext2_inode *inode);
#endif //INC_10_EXT2_FUSE_BLKITER_H