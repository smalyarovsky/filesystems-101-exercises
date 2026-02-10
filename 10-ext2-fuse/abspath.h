//
// Created by stepan on 2/10/26.
//

#ifndef INC_10_EXT2_FUSE_ABSPATH_H
#define INC_10_EXT2_FUSE_ABSPATH_H

#include <ext2fs/ext2_fs.h>
#include "ext2_util.h"

int abspath(const char *path, struct ext2_fs *fs);

#endif //INC_10_EXT2_FUSE_ABSPATH_H