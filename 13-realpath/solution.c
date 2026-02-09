#include <assert.h>
#include <errno.h>
#include <solution.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/stat.h>

#include "fs_malloc.h"

#define SYMLINK_JUMPS_MAX 40

static int abspath_split(const char *path, char comps[][NAME_MAX]) {
    int l = 0, r = 0, comps_len = 0;
    for (; r < (int) strnlen(path, PATH_MAX); ++r) {
        if (path[r] == '/') {
            if (l < r) {
                snprintf(comps[comps_len++], NAME_MAX, "%.*s", r - l, path + l);
            }
            l = r + 1;
        }
    }
    if (r > l) {
        snprintf(comps[comps_len++], NAME_MAX, "%.*s", r - l + 1, path + l);
    }
    return comps_len;
}

static void abspath_reverse(char comps[][NAME_MAX], const int comps_len) {
    char tmp[NAME_MAX];
    for (int i = 0; i < comps_len / 2; ++i) {
        memcpy(tmp, comps[i], NAME_MAX);
        memcpy(comps[i], comps[comps_len - 1 - i], NAME_MAX);
        memcpy(comps[comps_len - 1 - i], tmp, NAME_MAX);
    }
}

static void abspath_assemble(char *path, char comps[][NAME_MAX], const int comps_len) {
    path[0] = '/';
    path[1] = '\0';
    int path_len = 1;
    for (int i = 0; i < comps_len; ++i) {
        int comp_len = (int) strnlen(comps[i], PATH_MAX);
        strncat(path, comps[i], PATH_MAX - path_len);
        path_len += comp_len;
        if (i + 1 < comps_len) {
            strncat(path, "/", PATH_MAX - path_len);
            path_len++;
        }
    }
}

static void abspath_resolve(char *path) {
    char (*comps)[NAME_MAX] = fs_xmalloc(sizeof(char[PATH_MAX][NAME_MAX]));
    int comps_len = abspath_split(path, comps);
    while (comps_len > 0 && strncmp(comps[comps_len - 1], "/", PATH_MAX) == 0) {
        --comps_len;
    }
    abspath_assemble(path, comps, comps_len);
    free(comps);
}

struct abspath_state {
    int fd;
    int walked_len;
    char walked[PATH_MAX][NAME_MAX];
    int comps_len;
    char comps[PATH_MAX][NAME_MAX];
};

static void abspath_init(struct abspath_state *st, const char *path) {
    if ((st->fd = open("/", O_RDONLY)) < 0) {
        report_error("", "/", errno);
        return;
    }
    char resolved[PATH_MAX];
    snprintf(resolved, PATH_MAX, "%s", path);
    abspath_resolve(resolved);
    st->comps_len = abspath_split(resolved, st->comps);
    abspath_reverse(st->comps, st->comps_len);
    st->walked_len = 0;
}

void abspath(const char *path) {
    struct abspath_state *st = fs_xmalloc(sizeof(struct abspath_state));

    int bytes_read, is_dir = 1, errno_copy, fd;
    char tmp[PATH_MAX];

    abspath_init(st, path);
    while (st->comps_len > 0) {
        is_dir = 1;
        char *comp = st->comps[--st->comps_len];
        if (strncmp(".", comp, NAME_MAX) == 0) {
            continue;
        }
        if (strncmp("..", comp, NAME_MAX) == 0) {
            if (st->walked_len) {
                st->walked_len--;
                fd = st->fd;
                if ((st->fd = openat(st->fd, comp, O_RDONLY)) < 0) {
                    goto abspath_error;
                }
                close(fd);
            }
            continue;
        }
        fd = st->fd;
        if ((st->fd = openat(st->fd, comp, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) < 0) {
            if (errno == ENOTDIR) {
                is_dir = 0;
                if (st->comps_len != 0) {
                    goto abspath_error;
                }
                if ((st->fd = openat(fd, comp, O_RDONLY | O_NOFOLLOW)) < 0) {
                    goto abspath_error;
                }
            } else if (errno == ELOOP) {
                if ((bytes_read = (int) readlinkat(fd, comp, tmp, PATH_MAX)) > 0) {
                    st->fd = fd;
                    tmp[bytes_read] = '\0';
                    char comps[PATH_MAX][NAME_MAX];
                    int comps_len = abspath_split(tmp, comps);
                    abspath_reverse(comps, comps_len);
                    for (int j = 0; j < comps_len; ++j) {
                        snprintf(st->comps[st->comps_len++], NAME_MAX, "%s", comps[j]);
                    }
                    if (tmp[0] == '/') {
                        st->walked_len = 0;
                        if ((st->fd = open("/", O_RDONLY)) < 0) {
                            goto abspath_error;
                        }
                    }
                    continue;
                }
                goto abspath_error;
            } else {
                goto abspath_error;
            }
        }
        close(fd);
        snprintf(st->walked[st->walked_len++], NAME_MAX, "%s", comp);
    }
    abspath_assemble(tmp, st->walked, st->walked_len);
    if (is_dir) {
        strncat(tmp, "/", PATH_MAX - strnlen(tmp, PATH_MAX));
    }
    report_path(tmp);
    goto abspath_cleanup;
abspath_error:
    errno_copy = errno;
    abspath_assemble(tmp, st->walked, st->walked_len);
    report_error(tmp, tmp, errno_copy);
abspath_cleanup:
    if (st->fd >= 0) close(st->fd);
    free(st);
}
