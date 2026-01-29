#include <errno.h>
#include <solution.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/stat.h>

#define SYMLINK_JUMP_MAX 40

static int split(const char *path, char comps[][NAME_MAX]) {
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

static void reverse(char comps[][NAME_MAX], const int comps_len) {
    char tmp[NAME_MAX];
    for (int i = 0; i < comps_len / 2; ++i) {
        memcpy(tmp, comps[i], NAME_MAX);
        memcpy(comps[i], comps[comps_len - 1 - i], NAME_MAX);
        memcpy(comps[comps_len - 1 - i], tmp, NAME_MAX);
    }
}

static void assemble(char *path, char comps[][NAME_MAX], const int comps_len) {
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

static void presolve(char *path) {
    static char comps[PATH_MAX][NAME_MAX];
    int comps_len = split(path, comps);
    while (comps_len > 0 && strncmp(comps[comps_len - 1], "/", PATH_MAX) == 0) {
        --comps_len;
    }
    assemble(path, comps, comps_len);
}


struct pathwalker_state {
    int fd;
    int walked_len;
    char walked[PATH_MAX][NAME_MAX];
    int comps_len;
    char comps[PATH_MAX][NAME_MAX];
};

static void init(struct pathwalker_state *st, const char *path) {
    if ((st->fd = open("/", O_RDONLY)) < 0) {
        report_error("", "/", errno);
        return;
    }
    char resolved[PATH_MAX];
    snprintf(resolved, PATH_MAX, "%s", path);
    presolve(resolved);
    st->comps_len = split(resolved, st->comps);
    reverse(st->comps, st->comps_len);
    st->walked_len = 0;
}

void abspath(const char *path) {
    static struct pathwalker_state st;
    int read;
    char tmp[PATH_MAX];

    int isdir = 1;
    init(&st, path);
    while (st.comps_len > 0) {
        char *comp = st.comps[--st.comps_len];
        if (strncmp(".", comp, NAME_MAX) == 0) {
            continue;
        }
        if (strncmp("..", comp, NAME_MAX) == 0) {
            if (st.walked_len) {
                st.walked_len--;
                int fd = st.fd;
                if ((st.fd = openat(st.fd, comp, O_RDONLY)) < 0) {
                    if (errno == ENOTDIR) {
                        isdir = 0;
                    } else {
                        int errno_copy = errno;
                        assemble(tmp, st.walked, st.walked_len);
                        report_error(tmp, comp, errno_copy);
                        return;
                    }
                }
                close(fd);
            }
            continue;
        }
        if ((read = (int) readlinkat(st.fd, comp, tmp, PATH_MAX)) > 0) {
            tmp[read] = '\0';
            char comps[PATH_MAX][NAME_MAX];
            int comps_len = split(tmp, comps);
            reverse(comps, comps_len);
            for (int j = 0; j < comps_len; ++j) {
                snprintf(st.comps[st.comps_len++], NAME_MAX, "%s", comps[j]);
            }
            if (tmp[0] == '/') {
                st.walked_len = 0;
            }
            continue;
        }
        if (errno != EINVAL) {
            int errno_copy = errno;
            assemble(tmp, st.walked, st.walked_len);
            report_error(tmp, comp, errno_copy);
            return;
        }

        int fd = st.fd;
        int flag = O_RDONLY | O_NOFOLLOW;
        if (st.comps_len == 0) {
            flag |= O_DIRECTORY;
        }
        if ((st.fd = openat(st.fd, comp, flag)) < 0) {
            if (errno == ENOTDIR) {
                isdir = 0;
            } else {
                int errno_copy = errno;
                assemble(tmp, st.walked, st.walked_len);
                report_error(tmp, comp, errno_copy);
                return;
            }
        }
        close(fd);
        snprintf(st.walked[st.walked_len++], NAME_MAX, "%s", comp);
    }
    assemble(tmp, st.walked, st.walked_len);
    if (isdir) {
        strncat(tmp, "/", PATH_MAX - strnlen(tmp, PATH_MAX));
    }
    report_path(tmp);
}
