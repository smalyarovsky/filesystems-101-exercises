//
// Created by stepan on 2/10/26.
//

#include "ext2_util.h"

#include <fs_malloc.h>
#include <string.h>
#include <linux/limits.h>

int ext2_fs_init(struct ext2_fs **fs, int fd)
{
	*fs = fs_xmalloc(sizeof(struct ext2_fs));
	(*fs)->fd = fd;

	struct ext2_super_block sb;
	if (pread(fd, &sb, sizeof(struct ext2_super_block), 1024) < 0) {
		return -errno;
	}
	if (sb.s_magic != EXT2_SUPER_MAGIC) {
		return -EPROTO;
	}
	struct ext2_fs *f = *fs;
	f->block_size = 1024 << sb.s_log_block_size;
	f->inode_size = sb.s_inode_size;
	f->blocks_count = sb.s_blocks_count;
	f->blocks_per_group = sb.s_blocks_per_group;
	f->inodes_per_group = sb.s_inodes_per_group;
	f->groups_count = (f->blocks_count + f->blocks_per_group - 1) / f->blocks_per_group;
	f->inodes_count = sb.s_inodes_count;
	f->bgdt_size = f->groups_count * sizeof(struct ext2_group_desc);
	f->bgdt = fs_xmalloc(f->bgdt_size);
	uint32_t bgdt_first_block = (f->block_size > 1024 ? 1 : 2);
	if (pread(fd, (*fs)->bgdt, f->bgdt_size, bgdt_first_block * f->block_size) < 0) {
		return -errno;
	}
	return 0;
}

void ext2_fs_free(struct ext2_fs *fs)
{
	if (!fs) return;
	close(fs->fd);
	fs_xfree(fs->bgdt);
	fs_xfree(fs);
}

int ext2_blkiter_init(struct ext2_blkiter **i, struct ext2_fs *fs, uint32_t ino)
{
	*i = fs_xzalloc(sizeof(struct ext2_blkiter));
	if (0 >= ino || (ssize_t) ino > fs->inodes_count) {
		return -EINVAL;
	}
	uint32_t bg = (ino - 1) / fs->inodes_per_group;
	char inode_bitmap[fs->block_size];
	if (pread(fs->fd, inode_bitmap, fs->block_size, fs->bgdt[bg].bg_inode_bitmap * fs->block_size) < 0) {
		return -errno;
	}
	uint32_t offset = (ino - 1) % fs->inodes_per_group;
	int in_use = inode_bitmap[offset / 8] & (1 << (offset % 8));
	if (!in_use) {
		return -ENOENT;
	}
	struct ext2_inode *inode = fs_xmalloc(sizeof(struct ext2_inode));
	if (pread(fs->fd, inode, sizeof(struct ext2_inode),
		fs->bgdt[bg].bg_inode_table * fs->block_size + offset * fs->inode_size) < 0) {
		return -errno;
	}
	memcpy((*i)->layer[0], inode->i_block, 15 * sizeof(uint32_t));
	(*i)->block_size = fs->block_size;
	(*i)->fd = fs->fd;
	(*i)->ind = -1;
	(*i)->l1 = (*i)->l2 = (*i)->l3 = -1;
	(*i)->file_size = inode->i_size;
	fs_xfree(inode);
	return 0;
}


int ext2_blkiter_next(struct ext2_blkiter *i, int *blkno)
{
	const uint32_t per_block = i->block_size / sizeof(uint32_t);
	i->ind++;
	uint32_t ind = i->ind;
	if (ind < 12) {
		*blkno = i->layer[0][ind];
		return *blkno != 0;
	}
	ind -= 12;
	if (ind < per_block) {
		if (i->l1 != 12) {
			i->l1 = 12;
			if (i->layer[0][i->l1] == 0) {
				memset(i->layer[1], 0, i->block_size);
			} else if (pread(i->fd, i->layer[1], i->block_size, i->layer[0][i->l1] * i->block_size) < 0) {
				return -errno;
			}
		}
		*blkno = i->layer[1][ind];
		return 1;
	}
	ind -= per_block;
	if (ind < per_block * per_block) {
		if (i->l1 != 13) {
			i->l1 = 13;
			if (i->layer[0][i->l1] == 0) {
				memset(i->layer[1], 0, i->block_size);
			} else if (pread(i->fd, i->layer[1], i->block_size, i->layer[0][i->l1] * i->block_size) < 0) {
				return -errno;
			}
		}
		if (i->l2 != (int) (ind / per_block)) {
			i->l2 = (int) (ind / per_block);
			if (i->layer[1][i->l2] == 0) {
				memset(i->layer[2], 0, i->block_size);
			} else if (pread(i->fd, i->layer[2], i->block_size, i->layer[1][i->l2] * i->block_size) < 0) {
				return -errno;
			}
		}
		*blkno = i->layer[2][ind % per_block];
		return 1;
	}
	ind -= per_block * per_block;
	if (ind < per_block * per_block * per_block) {
		if (i->l1 != 14) {
			i->l1 = 14;
			if (i->layer[0][i->l1] == 0) {
				memset(i->layer[1], 0, i->block_size);
			} else if (pread(i->fd, i->layer[1],  i->block_size, i->layer[0][i->l1] * i->block_size) < 0) {
				return -errno;
			}
		}
		if (i->l2 != (int) (ind / per_block / per_block)) {
			i->l2 = (int) (ind / per_block / per_block);
			if (i->layer[1][i->l2] == 0) {
				memset(i->layer[2], 0, i->block_size);
			} else if (pread(i->fd, i->layer[2], i->block_size, i->layer[1][i->l2] * i->block_size) < 0) {
				return -errno;
			}
		}
		if (i->l3 != (int) ((ind / per_block) % per_block)) {
			i->l3 = (int) ((ind / per_block) % per_block);
			if (i->layer[2][i->l3] == 0) {
				memset(i->layer[3], 0, i->block_size);
			} else if (pread(i->fd, i->layer[3], i->block_size, i->layer[2][i->l3] * i->block_size) < 0) {
				return -errno;
			}
		}
		*blkno = i->layer[3][ind % per_block];
		return 1;
	}
	return 0;
}

void ext2_blkiter_free(struct ext2_blkiter *i)
{
	fs_xfree(i);
}

int ext2_openat(const uint32_t ino, const char *child, struct ext2_fs *fs, struct ext2_dir_entry_2 *result) {
	struct ext2_blkiter *it;
	int r;
	if ((r = ext2_blkiter_init(&it, fs, ino)) < 0) {
		return r;
	}
	int remaining = (int) it->file_size;
	while (remaining > 0) {
		int block;
		if ((r = ext2_blkiter_next(it, &block)) < 0) {
			ext2_blkiter_free(it);
			return r;
		}
		int to_read = (remaining < (int) fs->block_size) ? remaining : (int) fs->block_size;
		char buf[fs->block_size];
		if (block == 0) {
			memset(buf, 0, fs->block_size);
		} else if (pread(fs->fd, buf, to_read, block * fs->block_size) < 0) {
			int errno_copy = errno;
			ext2_blkiter_free(it);
			return -errno_copy;
		}
		int offset = 0;
		while (offset < to_read) {
			struct ext2_dir_entry_2 *dir_entry = (struct ext2_dir_entry_2 *) (buf + offset);
			char name[PATH_MAX];
			memcpy(name, dir_entry->name, dir_entry->name_len);
			name[dir_entry->name_len] = '\0';
			if (strncmp(name, child, dir_entry->name_len + 1) == 0) {
				memcpy(result, dir_entry, sizeof(struct ext2_dir_entry_2));
				ext2_blkiter_free(it);
				return 0;
			}
			offset += dir_entry->rec_len;
		}
		remaining -= to_read;
	}
	ext2_blkiter_free(it);
	return -ENOENT;
}

int ext2_readlink(const uint32_t ino, struct ext2_fs *fs, char *result) {
	struct ext2_blkiter *it;
	int r;
	if ((r = ext2_blkiter_init(&it, fs, ino)) < 0) {
		return r;
	}
	if (it->file_size <= 60) {
		memcpy(result, it->layer[0], it->file_size);
		result[it->file_size] = '\0';
	} else {
		int remaining = (int) it->file_size, written = 0;
		while (remaining > 0) {
			int block;
			if ((r = ext2_blkiter_next(it, &block)) < 0) {
				ext2_blkiter_free(it);
				return r;
			}
			int to_read = (remaining < (int) fs->block_size) ? remaining : (int) fs->block_size;
			char buf[fs->block_size];
			if (block == 0) {
				memset(buf, 0, fs->block_size);
			} else if (pread(fs->fd, buf, to_read, block * fs->block_size) < 0) {
				int errno_copy = errno;
				ext2_blkiter_free(it);
				return -errno_copy;
			}
			memcpy(result + written, buf, to_read);
			written += to_read;
			remaining -= to_read;
		}
		result[written] = '\0';
	}
	ext2_blkiter_free(it);
	return 0;
}

int ext2_readinode(const uint32_t ino, struct ext2_fs *fs, struct ext2_inode *inode) {
	if (0 >= ino || (ssize_t) ino > fs->inodes_count) {
		return -EINVAL;
	}
	uint32_t bg = (ino - 1) / fs->inodes_per_group;
	char inode_bitmap[fs->block_size];
	if (pread(fs->fd, inode_bitmap, fs->block_size, fs->bgdt[bg].bg_inode_bitmap * fs->block_size) < 0) {
		return -errno;
	}
	uint32_t offset = (ino - 1) % fs->inodes_per_group;
	int in_use = inode_bitmap[offset / 8] & (1 << (offset % 8));
	if (!in_use) {
		return -ENOENT;
	}
	if (pread(fs->fd, inode, sizeof(struct ext2_inode),
		fs->bgdt[bg].bg_inode_table * fs->block_size + offset * fs->inode_size) < 0) {
		return -errno;
	}
	return 0;
}