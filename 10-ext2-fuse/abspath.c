#include "abspath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include "ext2_util.h"
#include "fs_malloc.h"

static int abspath_split(const char *path, char comps[][NAME_MAX]) {
    int l = 0, r = 0, comps_len = 0;
    for (int path_len = (int) strnlen(path, PATH_MAX); r < path_len; ++r) {
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
        strncat(path + path_len, comps[i], PATH_MAX - path_len);
        path_len += comp_len;
        if (i + 1 < comps_len) {
            strncat(path + path_len, "/", PATH_MAX - path_len);
            path_len++;
        }
    }
}

static void abspath_resolve(char *path) {
    char (*comps)[NAME_MAX] = fs_xzalloc(sizeof(char[PATH_MAX][NAME_MAX]));
    int comps_len = abspath_split(path, comps);
    while (comps_len > 0 && strncmp(comps[comps_len - 1], "/", PATH_MAX) == 0) {
        --comps_len;
    }
    abspath_assemble(path, comps, comps_len);
    free(comps);
}

struct abspath_state {
    uint32_t ino;
    int walked_len;
    char walked[PATH_MAX][NAME_MAX];
    int comps_len;
    char comps[PATH_MAX][NAME_MAX];
};

int abspath(const char *path, struct ext2_fs *fs) {
    struct abspath_state *st = fs_xzalloc(sizeof(struct abspath_state));

    char *comp;
    int r;
    char tmp[PATH_MAX];


    struct ext2_dir_entry_2 dir_entry;
    if ((r = ext2_openat(2, ".", fs, &dir_entry)) < 0) {
        free(st);
        return r;
    }

    st->ino = 2;
    snprintf(tmp, PATH_MAX, "%s", path);
    abspath_resolve(tmp);
    st->comps_len = abspath_split(tmp, st->comps);
    abspath_reverse(st->comps, st->comps_len);
    st->walked_len = 0;

    while (st->comps_len > 0) {
        if (st->ino != 2 && dir_entry.file_type != EXT2_FT_DIR) {
            free(st);
            return -ENOTDIR;
        }

        comp = st->comps[--st->comps_len];
        if (strncmp(".", comp, NAME_MAX) == 0) {
            continue;
        }
        if (strncmp("..", comp, NAME_MAX) == 0) {
            if (st->walked_len) {
                st->walked_len--;
                if ((r = ext2_openat(st->ino, comp, fs, &dir_entry)) < 0) {
                    free(st);
                    return r;
                }
                st->ino = dir_entry.inode;
            }
            continue;
        }
        if ((r = ext2_openat(st->ino, comp, fs, &dir_entry)) < 0) {
            free(st);
            return r;
        }
        if (dir_entry.file_type == EXT2_FT_SYMLINK) {
            if ((r = ext2_readlink(dir_entry.inode, fs, tmp)) < 0) {
                free(st);
                return r;
            }
            char (*comps)[NAME_MAX] = fs_xmalloc(sizeof(char[PATH_MAX][NAME_MAX]));
            int comps_len = abspath_split(tmp, comps);
            abspath_reverse(comps, comps_len);
            for (int j = 0; j < comps_len; ++j) {
                snprintf(st->comps[st->comps_len++], NAME_MAX, "%s", comps[j]);
            }
            free(comps);
            if (tmp[0] == '/') {
                st->walked_len = 0;
                st->ino = 2;
                if ((r = ext2_openat(2, ".", fs, &dir_entry)) < 0) {
                    free(st);
                    return r;
                }

            }
            continue;
        }
        st->ino = dir_entry.inode;
        snprintf(st->walked[st->walked_len++], NAME_MAX, "%s", comp);
    }
    return st->ino;
}
