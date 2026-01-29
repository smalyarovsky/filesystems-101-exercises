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


static int psplit(char *path, char *comps[]) {
    int cur = 0;
    for (int i = 0; i < PATH_MAX; ++i) {
        if (path[i] == '\0') {
            break;
        }
        if (path[i] == '/') {
            path[i] = '\0';
            if (i + 1 < PATH_MAX && path[i + 1] != '\0') {
                comps[cur++] = path + i + 1;
            }
        }
    }
    return cur;
}

static void passemble(char *path, char *comps[], int comps_len) {
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
    char path_copy[PATH_MAX];
    snprintf(path_copy, PATH_MAX, "%s/", path);
    char *comps[PATH_MAX];
    int comps_len = psplit(path_copy, comps);

    int comps_cur = 1;
    char *comps_stable[PATH_MAX] = {"/"};

    for (int i = 0; i < comps_len; ++i) {
        if (strncmp(".", comps[i], PATH_MAX) == 0) {
            continue;
        }
        if (strncmp("..", comps[i], PATH_MAX) == 0 && comps_cur > 1) {
            comps_cur--;
            continue;
        }
        if (strncmp("/", comps_stable[comps_cur - 1], PATH_MAX) == 0 && strncmp("/", comps[i], PATH_MAX) == 0) {
            continue;
        }
        comps_stable[comps_cur++] = comps[i];
    }

    while (comps_cur > 1 && strncmp(comps_stable[comps_cur - 1], "/", PATH_MAX) == 0) {
        --comps_cur;
    }

    passemble(path, comps_stable, comps_cur);
}


struct pjumper_state {
    int fd;
    char cur_path[2 * PATH_MAX];
    char init_path[2 * PATH_MAX];
    int comps_len;
    char *comps[PATH_MAX];
};

static void init(struct pjumper_state *st, const char *path) {
    if ((st->fd = open("/", O_RDONLY)) < 0) {
        report_error("", "/", errno);
    }
    st->cur_path[0] = '\0';
    snprintf(st->init_path, PATH_MAX, "%s", path);
    presolve(st->init_path);
    st->comps_len = psplit(st->init_path, st->comps);
}



void abspath(const char *path) {

    struct pjumper_state st;
    init(&st, path);

    char cur_path_copy[PATH_MAX];
    for (int i = 0; i < st.comps_len; ++i) {
        snprintf(cur_path_copy, PATH_MAX, "%s/", st.cur_path);

        char *comp = st.comps[i];

        strncat(st.cur_path, "/", PATH_MAX);
        strncat(st.cur_path, comp, PATH_MAX);

        struct stat stat;
        if (lstat(st.cur_path, &stat) == -1) {
            report_error(cur_path_copy, comp, errno);
            return;
        }

        if (S_ISLNK(stat.st_mode)) {
            char link[PATH_MAX];
            int len = (int) readlink(st.cur_path, link, PATH_MAX - 1);
            if (len < 0) {
                report_error(cur_path_copy, comp, errno);
            }
            link[len] = '\0';

            if (link[0] == '/') {
                snprintf(st.cur_path, PATH_MAX, "%s", link);
                presolve(st.cur_path);
            } else {
                strncat(st.cur_path, "/", PATH_MAX);
                strncat(st.cur_path, link, PATH_MAX);
                presolve(st.cur_path);
            }
        }

        if ((st.fd = openat(st.fd, comp, O_RDONLY)) < 0) {
            report_error(cur_path_copy, comp, errno);
        }
    }
    struct stat stat;
    if (lstat(st.cur_path, &stat) == -1) {
        report_error(cur_path_copy, "", errno);
        return;
    }
    if (S_ISDIR(stat.st_mode)) {
        strncat(st.cur_path, "/", PATH_MAX);
    }
    report_path(st.cur_path);
}
