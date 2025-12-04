#include <solution.h>
#include <fs_malloc.h>
#include <ext2fs/ext2_fs.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

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
};

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

int ext2_blkiter_init(struct ext2_blkiter **i, struct ext2_fs *fs, int ino)
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
			if (i->layer[0][i->l1] == 0) return 0;
			if (pread(i->fd, i->layer[1], i->block_size, i->layer[0][i->l1] * i->block_size) < 0) {
				return -errno;
			}
		}
		*blkno = i->layer[1][ind];
		return *blkno != 0;
	}
	ind -= per_block;
	if (ind < per_block * per_block) {
		if (i->l1 != 13) {
			i->l1 = 13;
			if (i->layer[0][i->l1] == 0) return 0;
			if (pread(i->fd, i->layer[1], i->block_size, i->layer[0][i->l1] * i->block_size) < 0) {
				return -errno;
			}
		}
		if (i->l2 != (int) (ind / per_block)) {
			i->l2 = (int) (ind / per_block);
			if (i->layer[1][i->l2] == 0) return 0;
			if (pread(i->fd, i->layer[2], i->block_size, i->layer[1][i->l2] * i->block_size) < 0) {
				return -errno;
			}
		}
		*blkno = i->layer[2][ind % per_block];
		return *blkno != 0;
	}
	ind -= per_block * per_block;
	if (ind < per_block * per_block * per_block) {
		if (i->l1 != 14) {
			i->l1 = 14;
			if (i->layer[0][i->l1] == 0) return 0;
			if (pread(i->fd, i->layer[1],  i->block_size, i->layer[0][i->l1] * i->block_size) < 0) {
				return -errno;
			}
		}
		if (i->l2 != (int) (ind / per_block / per_block)) {
			i->l2 = (int) (ind / per_block / per_block);
			if (i->layer[1][i->l2] == 0) return 0;
			if (pread(i->fd, i->layer[2], i->block_size, i->layer[1][i->l2] * i->block_size) < 0) {
				return -errno;
			}
		}
		if (i->l3 != (int) ((ind / per_block) % per_block)) {
			i->l3 = (int) ((ind / per_block) % per_block);
			if (i->layer[2][i->l3] == 0) return 0;
			if (pread(i->fd, i->layer[3], i->block_size, i->layer[2][i->l3] * i->block_size) < 0) {
				return -errno;
			}
		}
		*blkno = i->layer[3][ind % per_block];
		return *blkno != 0;
	}
	return 0;
}

void ext2_blkiter_free(struct ext2_blkiter *i)
{
	fs_xfree(i);
}

int dump_file(int img, int inode_nr, int out)
{
	struct ext2_fs *fs;
	struct ext2_blkiter *it;
	int r;

	if ((r = ext2_fs_init(&fs, img)) < 0)
		return r;

	if ((r = ext2_blkiter_init(&it, fs, inode_nr)) < 0) {
		ext2_fs_free(fs);
		return r;
	}

	for (;;) {
		int blkno;
		r = ext2_blkiter_next(it, &blkno);

		if (r < 0) {
			ext2_blkiter_free(it);
			ext2_fs_free(fs);
			return r;
		}
		if (r == 0) {
			break;
		}
		char buf[fs->block_size];
		off_t off = (off_t)blkno * fs->block_size;
		if (pread(img, buf, fs->block_size, off) < 0) {
			ext2_blkiter_free(it);
			ext2_fs_free(fs);
			return -errno;
		}
		if (write(out, buf, fs->block_size) < 0) {
			ext2_blkiter_free(it);
			ext2_fs_free(fs);
			return -errno;
		}
	}

	ext2_blkiter_free(it);
	ext2_fs_free(fs);
	return 0;
}
