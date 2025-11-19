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
        snprintf(buf, PATH_MAX, "%s", parent);
        return 0;
    }
    if (strncmp(child, "..", PATH_MAX) == 0) {
        snprintf(buf, PATH_MAX, "%s", parent);
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
    snprintf(buf, PATH_MAX, "%s", tmp);

    if (buf[0] != '/') {
        char resolved[PATH_MAX];
        snprintf(resolved, PATH_MAX, "%s/%s", parent[0] ? parent : "/", buf);
        snprintf(buf, PATH_MAX, "%s", resolved);
    }
    return 1;
}

static void finalize(char *path) {
    if (path[0] != '/') {
        char tmp[PATH_MAX];
        snprintf(tmp, PATH_MAX, "/%s", path);
        snprintf(path, PATH_MAX, "%s", tmp);
    }

    struct stat st;

    if (stat(path, &st) != 0) {
        exit(1);
    }

    if (S_ISDIR(st.st_mode)) {
        int len = strnlen(path, PATH_MAX - 2);
        if (path[len - 1] != '/') {
            path[len] = '/';
            path[len + 1] = '\0';
        }
        report_path(path);
    } else {
        report_path(path);
    }
}

void abspath(const char *path) {

    size_t childlen = 0;
    char parent[PATH_MAX], child[PATH_MAX], tmp[PATH_MAX];
    parent[0] = '\0';

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
            snprintf(parent, PATH_MAX, "%s", tmp);
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
        snprintf(parent, PATH_MAX, "%s", tmp);
        childlen = 0;
    }
    finalize(parent);
}
