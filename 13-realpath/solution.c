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

static int psplit(const char *path, char comps[][NAME_MAX]) {
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
    char tmp[comps_len][NAME_MAX];
    for (int i = 0; i < comps_len; ++i) {
        snprintf(tmp[i], NAME_MAX, "%s", comps[i]);
    }
    for (int i = 0; i < comps_len; ++i) {
        snprintf(comps[comps_len - i - 1], NAME_MAX, "%s", tmp[i]);
    }
}

static void passemble(char *path, char comps[][NAME_MAX], const int comps_len) {
    path[0] = '\0';
    int path_len = 0;
    for (int i = 0; i < comps_len; ++i) {
        int comp_len = (int) strnlen(comps[i], PATH_MAX);
        strncat(path, comps[i], PATH_MAX - path_len);
        path_len += comp_len;
        if (0 < i && i + 1 < comps_len) {
            strncat(path, "/", PATH_MAX - path_len);
            path_len++;
        }
    }
}

static void presolve(char *path) {
    char comps[PATH_MAX][NAME_MAX];
    int comps_len = psplit(path, comps);

    int comps_cur = 1;
    char comps_stable[PATH_MAX][NAME_MAX] = {"/"};

    for (int i = 0; i < comps_len; ++i) {
        if (strncmp(".", comps[i], PATH_MAX) == 0) {
            continue;
        }
        if (strncmp("..", comps[i], PATH_MAX) == 0) {
            if (comps_cur > 1) comps_cur--;
            continue;
        }
        if (strncmp("/", comps_stable[comps_cur - 1], PATH_MAX) == 0 && strncmp("/", comps[i], PATH_MAX) == 0) {
            continue;
        }
        snprintf(comps_stable[comps_cur++], NAME_MAX, "%s", comps[i]);
    }

    while (comps_cur > 1 && strncmp(comps_stable[comps_cur - 1], "/", PATH_MAX) == 0) {
        --comps_cur;
    }

    passemble(path, comps_stable, comps_cur);
}


struct pjumper_state {
    int fd;
    char path[PATH_MAX];
    int comps_len;
    char comps[PATH_MAX][NAME_MAX];
};

static void init(struct pjumper_state *st, const char *path) {
    if ((st->fd = open("/", O_RDONLY)) < 0) {
        report_error("", "/", errno);
    }
    st->path[0] = '\0';
    char resolved[PATH_MAX];
    snprintf(resolved, PATH_MAX, "%s", path);
    presolve(resolved);
    st->comps_len = psplit(resolved, st->comps);
    reverse(st->comps, st->comps_len);
}

void abspath(const char *path) {
    struct pjumper_state st;
    init(&st, path);

    char prev_path[PATH_MAX];
    while (st.comps_len > 0) {
        snprintf(prev_path, PATH_MAX, "%s", st.path);

        char *comp = st.comps[st.comps_len - 1];

        int len = (int) strnlen(comp, PATH_MAX);
        strncat(st.path, "/", PATH_MAX - len);
        strncat(st.path, comp, PATH_MAX - len - 1);

        struct stat stat;
        if (lstat(st.path, &stat) == -1) {
            report_error(prev_path, comp, errno);
            return;
        }

        if (S_ISLNK(stat.st_mode)) {
            char link[PATH_MAX];
            int len = (int) readlink(st.path, link, PATH_MAX - 1);
            if (len < 0) {
                report_error(prev_path, comp, errno);
            }
            link[len] = '\0';

            presolve(link);
            char comps[PATH_MAX][NAME_MAX];
            int comps_len = psplit(link, comps);
            reverse(comps, comps_len);

            if (link[0] == '/') {
                st.comps_len = comps_len;
                for (int j = 0; j < comps_len; ++j) {
                    snprintf(st.comps[j], NAME_MAX, "%s", comps[j]);
                }
            } else {
                for (int j = 0; j < comps_len; ++j) {
                    snprintf(st.comps[st.comps_len++], NAME_MAX, "%s", comps[j]);
                }
            }
            continue;
        }
        int fd = st.fd;
        if ((st.fd = openat(st.fd, comp, O_RDONLY | O_NOFOLLOW)) < 0) {
            report_error(prev_path, comp, errno);
        }
        close(fd);
        st.comps_len--;
    }
    snprintf(prev_path, PATH_MAX, "%s", st.path);
    struct stat stat;
    if (lstat(st.path, &stat) == -1) {
        report_error(prev_path, "", errno);
        return;
    }
    if (S_ISDIR(stat.st_mode)) {
        strncat(st.path, "/", PATH_MAX - strnlen(st.path, PATH_MAX));
    }
    report_path(st.path);
}
