#include <solution.h>

#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <abspath.h>

#include "ext2_util.h"
#include "abspath.h"

static int ext2_img;
static struct ext2_fs *fs;

static int ext2_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
	(void) fi;

	int r;

	int ino = abspath(path, fs);
	if (ino < 0) {
		return ino;
	}
	struct ext2_inode inode;
	if ((r = ext2_readinode(ino, fs, &inode)) < 0) {
		return r;
	}
	stbuf->st_ino   = ino;
	stbuf->st_mode  = inode.i_mode;
	stbuf->st_nlink = inode.i_links_count;
	stbuf->st_uid   = inode.i_uid;
	stbuf->st_gid   = inode.i_gid;
	stbuf->st_size  = inode.i_size;

	stbuf->st_atime = inode.i_atime;
	stbuf->st_mtime = inode.i_mtime;
	stbuf->st_ctime = inode.i_ctime;

	stbuf->st_blksize = fs->block_size;
	stbuf->st_blocks  = inode.i_blocks;

	return 0;
}

static int ext2_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
						   struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
	(void) offset;
	(void) fi;
	(void) flags;
	int r;

	int ino = abspath(path, fs);
	if (ino < 0) {
		return ino;
	}
	struct ext2_inode inode;
	if ((r = ext2_readinode(ino, fs, &inode)) < 0) {
		return r;
	}
	if (!S_ISDIR(inode.i_mode)) return -ENOTDIR;


	struct ext2_blkiter *it;
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
		char buff[fs->block_size];
		if (block == 0) {
			memset(buff, 0, fs->block_size);
		} else if (pread(fs->fd, buff, to_read, block * fs->block_size) < 0) {
			int errno_copy = errno;
			ext2_blkiter_free(it);
			return -errno_copy;
		}
		int offfset = 0;
		while (offfset < to_read) {
			struct ext2_dir_entry_2 *dir_entry = (struct ext2_dir_entry_2 *) (buff + offfset);
			if (dir_entry->inode > 0) {
				char name[PATH_MAX];
				memcpy(name, dir_entry->name, dir_entry->name_len);
				name[dir_entry->name_len] = '\0';

				struct stat st;
				memset(&st, 0, sizeof(st));
				st.st_ino = dir_entry->inode;

				if (dir_entry->file_type == EXT2_FT_UNKNOWN) {
					struct ext2_inode tinode;
					if ((r = ext2_readinode(dir_entry->inode, fs, &tinode)) < 0) {
						ext2_blkiter_free(it);
						return r;
					}
					st.st_mode = tinode.i_mode;
				} else if (dir_entry->file_type == EXT2_FT_DIR) st.st_mode = S_IFDIR;
				else if (dir_entry->file_type == EXT2_FT_REG_FILE) st.st_mode = S_IFREG;
				else if (dir_entry->file_type == EXT2_FT_SYMLINK) st.st_mode = S_IFLNK;
				else if (dir_entry->file_type == EXT2_FT_CHRDEV) st.st_mode = S_IFCHR;
				else if (dir_entry->file_type == EXT2_FT_BLKDEV) st.st_mode = S_IFBLK;
				else if (dir_entry->file_type == EXT2_FT_FIFO) st.st_mode = S_IFIFO;
				else if (dir_entry->file_type == EXT2_FT_SOCK) st.st_mode = S_IFSOCK;

				filler(buf, name, &st, 0,0);
			}
			offfset += dir_entry->rec_len;
		}
		remaining -= to_read;
	}
	ext2_blkiter_free(it);
	return 0;
}

static int ext2_open(const char *path, struct fuse_file_info *fi) {
	int r;
	int ino = abspath(path, fs);
	if (ino < 0) {
		return ino;
	}
	struct ext2_inode inode;
	if ((r = ext2_readinode(ino, fs, &inode)) < 0) {
		return r;
	}
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EROFS;

	return 0;
}

static int ext2_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void)fi;
	int r;

	int ino = abspath(path, fs);
	if (ino < 0) {
		return ino;
	}

	struct ext2_inode inode;
	if ((r = ext2_readinode(ino, fs, &inode)) < 0) {
		return r;
	}

	if (offset < 0) {
		return -EINVAL;
	}
	if ((uint64_t)offset >= (uint64_t)inode.i_size || size == 0) {
		return 0;
	}

	uint64_t avail = (uint64_t)inode.i_size - (uint64_t)offset;
	if (size > avail) {
		size = avail;
	}

	struct ext2_blkiter *it;
	if ((r = ext2_blkiter_init(&it, fs, (uint32_t)ino)) < 0) {
		return r;
	}

	uint32_t bs = fs->block_size;
	uint64_t skip = (uint64_t)offset;

	while (skip >= bs) {
		int block;
		if ((r = ext2_blkiter_next(it, &block)) < 0) {
			ext2_blkiter_free(it);
			return r;
		}
		skip -= bs;
	}

	size_t written = 0;
	size_t remaining = size;

	while (remaining > 0) {
		int block;
		if ((r = ext2_blkiter_next(it, &block)) < 0) {
			ext2_blkiter_free(it);
			return r;
		}

		size_t start = (size_t)skip;
		size_t can = bs - start;
		size_t to_read = remaining < can ? remaining : can;

		if (block == 0) {
			memset(buf + written, 0, to_read);
		} else if (pread(fs->fd, buf + written, to_read, (off_t)block * (off_t)bs + (off_t)start) < 0) {
			int errno_copy = errno;
			ext2_blkiter_free(it);
			return -errno_copy;
		}

		written += to_read;
		remaining -= to_read;
		skip = 0;
	}

	ext2_blkiter_free(it);
	return (int)written;
}


static int ext2_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	(void) path;
	(void) buf;
	(void) size;
	(void) offset;
	(void) fi;
	return -EROFS;
}

static void *ext2_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	(void) conn;
	cfg->kernel_cache = 1;
	ext2_fs_init(&fs, ext2_img);
	return NULL;
}

static void ext2_destroy(void *private_data) {
	(void) private_data;
	ext2_fs_free(fs);
}

static int ext2_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
	(void) path; (void) size; (void) fi;
	return -EROFS;
}

static int ext2_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	(void) path; (void) mode; (void) fi;
	return -EROFS;
}

static int ext2_unlink(const char *path) {
	(void) path;
	return -EROFS;
}

static int ext2_mkdir(const char *path, mode_t mode) {
	(void) path; (void) mode;
	return -EROFS;
}

static int ext2_rmdir(const char *path) {
	(void) path;
	return -EROFS;
}

static int ext2_rename(const char *from, const char *to, unsigned int flags) {
	(void) from; (void) to; (void) flags;
	return -EROFS;
}

static int ext2_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
	(void) path; (void) mode; (void) fi;
	return -EROFS;
}

static int ext2_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
	(void) path; (void) uid; (void) gid; (void) fi;
	return -EROFS;
}

static int ext2_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
	(void) path; (void) tv; (void) fi;
	return -EROFS;
}

static int ext2_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
	(void) path; (void) name; (void) value; (void) size; (void) flags;
	return -EROFS;
}

static int ext2_removexattr(const char *path, const char *name) {
	(void) path; (void) name;
	return -EROFS;
}

static const struct fuse_operations ext2_ops = {
	.getattr    = ext2_getattr,
	.readdir    = ext2_readdir,
	.open       = ext2_open,
	.read       = ext2_read,
	.write      = ext2_write,
	.init       = ext2_init,
	.truncate   = ext2_truncate,
	.create     = ext2_create,
	.unlink     = ext2_unlink,
	.mkdir      = ext2_mkdir,
	.rmdir      = ext2_rmdir,
	.rename     = ext2_rename,
	.chmod      = ext2_chmod,
	.chown      = ext2_chown,
	.utimens    = ext2_utimens,
	.setxattr   = ext2_setxattr,
	.removexattr= ext2_removexattr,
	.destroy    = ext2_destroy,
};

int ext2fuse(int img, const char *mntp)
{
	ext2_img = img;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
