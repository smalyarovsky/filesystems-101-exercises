#include <solution.h>

#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

void lsof(void)
{
	struct dirent *pDirent, *pDirentFd;
	DIR *pDir = opendir("/proc"), *pDirFd;
	if (pDir == NULL) {
		report_error("/proc", errno);
		return;
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		char *endptr;
		strtol(pDirent->d_name, &endptr, 10);
		if (*endptr != '\0') {
			continue;
		}
		char path[PATH_MAX];
		snprintf(path, PATH_MAX, "/proc/%s/fd", pDirent->d_name);


		if ((pDirFd = opendir(path)) == NULL) {
			report_error(path, errno);
			continue;
		}
		while ((pDirentFd = readdir(pDirFd)) != NULL) {
			char buffer[PATH_MAX], curpath[PATH_MAX];
			snprintf(curpath, PATH_MAX, "/proc/%s/fd/%s", pDirent->d_name, pDirentFd->d_name);
			realpath(curpath, buffer);
			if (buffer[0] != '/') continue;
			report_file(buffer);
		}
		closedir(pDirFd);
	}
	closedir(pDir);
}
