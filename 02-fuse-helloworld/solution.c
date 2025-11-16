#include <solution.h>

#include <fuse.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#include <stdio.h>
#include <fcntl.h>
#include <stddef.h>

static int hellofs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
	(void) fi;

	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, "/hello") == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = 1024;
	} else {
		res = -ENOENT;
	}
	return res;
}

static int hellofs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
						   struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
	(void) offset;
	(void) fi;
	(void) flags;

	if (strcmp(path, "/") != 0) {
		return -ENOENT;
	}

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, "hello", NULL, 0, 0);
	return 0;
}

static int hellofs_open(const char *path, struct fuse_file_info *fi) {
	if (strcmp(path, "/hello") != 0)
		return -ENOENT;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EROFS;

	return 0;
}

static int hellofs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	(void) fi;
	if (strcmp(path, "/hello") != 0) {
		return -ENOENT;
	}
	pid_t pid = fuse_get_context()->pid;
	char res[32];
	sprintf(res, "hello, %d\n", pid);
	const size_t len = strlen(res);
	if ((size_t) offset < len) {
		if (offset + size > len) {
			size = len - offset;
		}
		memcpy(buf, res + offset, size);
	} else {
		size = 0;
	}
	return (int) size;
}

static int hellofs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	(void) path;
	(void) buf;
	(void) size;
	(void) offset;
	(void) fi;
	return -EROFS;
}

static void *hellofs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	(void) conn;
	cfg->kernel_cache = 1;
	return NULL;
}

static int hellofs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
	(void) path; (void) size; (void) fi;
	return -EROFS;
}

static int hellofs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	(void) path; (void) mode; (void) fi;
	return -EROFS;
}

static int hellofs_unlink(const char *path) {
	(void) path;
	return -EROFS;
}

static int hellofs_mkdir(const char *path, mode_t mode) {
	(void) path; (void) mode;
	return -EROFS;
}

static int hellofs_rmdir(const char *path) {
	(void) path;
	return -EROFS;
}

static int hellofs_rename(const char *from, const char *to, unsigned int flags) {
	(void) from; (void) to; (void) flags;
	return -EROFS;
}

static int hellofs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
	(void) path; (void) mode; (void) fi;
	return -EROFS;
}

static int hellofs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
	(void) path; (void) uid; (void) gid; (void) fi;
	return -EROFS;
}

static int hellofs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
	(void) path; (void) tv; (void) fi;
	return -EROFS;
}

static int hellofs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {
	(void) path; (void) name; (void) value; (void) size; (void) flags;
	return -EROFS;
}

static int hellofs_removexattr(const char *path, const char *name) {
	(void) path; (void) name;
	return -EROFS;
}

static const struct fuse_operations hellofs_ops = {
	.getattr    = hellofs_getattr,
	.readdir    = hellofs_readdir,
	.open       = hellofs_open,
	.read       = hellofs_read,
	.write      = hellofs_write,
	.init       = hellofs_init,
	.truncate   = hellofs_truncate,
	.create     = hellofs_create,
	.unlink     = hellofs_unlink,
	.mkdir      = hellofs_mkdir,
	.rmdir      = hellofs_rmdir,
	.rename     = hellofs_rename,
	.chmod      = hellofs_chmod,
	.chown      = hellofs_chown,
	.utimens    = hellofs_utimens,
	.setxattr   = hellofs_setxattr,
	.removexattr= hellofs_removexattr,
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
