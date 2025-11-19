#include <assert.h>
#include <errno.h>
#include <solution.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <asm-generic/errno-base.h>
#include <linux/limits.h>


static int jump(char *parent, char *child, char *buf) {
    if (strncmp(child, ".", PATH_MAX) == 0) {
        strncpy(buf, parent, PATH_MAX);
        return 0;
    }
    if (strncmp(child, "..", PATH_MAX) == 0) {
        strncpy(buf, parent, PATH_MAX);
        for (int i = (int) strnlen(buf, PATH_MAX) - 1; i >= 0; --i) {
            if (buf[i] == '/' && i == 0) {
                buf[i] = '\0';
            }
        }
        return 0;
    }
    snprintf(buf, PATH_MAX, "%s/%s", parent, child);
    char tmp[PATH_MAX];
    const int nbytes = (int) readlink(buf, tmp, PATH_MAX - 1);
    if (nbytes < 0) {
        if (errno == EINVAL) {
            return 0;
        }
        report_error(parent, child, errno);
    }
    tmp[nbytes] = '\0';
    strncpy(buf, tmp, PATH_MAX);
    return 1;
}

static void finalize(char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        exit(1);
    }

    if (S_ISDIR(st.st_mode)) {
        int len = strnlen(path, PATH_MAX - 2);
        path[len] = '/';
        path[len + 1] = '\0';
        report_path(path);
    } else {
        report_path(path);
    }
}

void abspath(const char *path) {

    size_t childlen = 0;
    char parent[PATH_MAX], child[PATH_MAX], tmp[PATH_MAX];
    strncpy(parent, "", PATH_MAX);

    const size_t len = strnlen(path, PATH_MAX);


    for (size_t i = 0; i < len; ++i) {
        if (i == 0 && path[i] == '/') {
            continue;
        }
        if (path[i] == '/') {
            child[childlen] = '\0';
            tmp[0] = '\0';
            if (jump(parent, child, tmp)) {
                abspath(tmp);
            }
            strncpy(parent, tmp, PATH_MAX);
            childlen = 0;
        } else {
            child[childlen++] = path[i];
        }
    }
    if (childlen != 0) {
        child[childlen] = '\0';
        tmp[0] = '\0';
        if (jump(parent, child, tmp)) {
            abspath(tmp);
        }
        strncpy(parent, tmp, PATH_MAX);
        childlen = 0;
    }
    finalize(parent);
}
