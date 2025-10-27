#include <ctype.h>
#include <solution.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>


void ps(void) {
	int fd;
	DIR *pDir;
	struct dirent *pDirent;
 	pDir = opendir("/proc");
	if (pDir == NULL) {
		report_error("/proc", errno);
		return;
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		int isPid = 1;
		for (int i = 0; pDirent->d_name[i] != '\0'; i++) {
			if (!isdigit(pDirent->d_name[i])) {
				isPid = 0;
				break;
			}
		}
		if (!isPid) continue;

		int pid = atoi(pDirent->d_name);

		char path[256] = "/proc/";
		strncat(path, pDirent->d_name, 255 - 6 - strlen(pDirent->d_name));

		int len = strlen(path);

		strncat(path, "/exe", 255 - len - 4);
		char exe[PATH_MAX];
		if (realpath(path, exe) == NULL) {
			report_error(path, errno);
			continue;
		}
		path[len] = '\0';

		strncat(path, "/cmdline", 255 - len - 8);
		char cmdline[4096];
		if ((fd = open(path, O_RDONLY)) == -1) {
			report_error(path, errno);
			continue;
		}
		int bytesRead = read(fd, cmdline, 4096);
		if (bytesRead == -1) {
			report_error(path, errno);
			continue;
		}
		cmdline[bytesRead] = '\0';
		close(fd);
		path[len] = '\0';

		char *argv[4096];
		int j = 0;
		for (int i = 0; cmdline[i] != '\0'; j++) {
			argv[j] = cmdline + i;
			i += strlen(cmdline + i) + 1;
		}
		argv[j] = NULL;
		path[len] = '\0';


		strncat(path, "/environ", 255 - len - 8);
		char env[4096];
		if ((fd = open(path, O_RDONLY)) == -1) {
			report_error(path, errno);
			continue;
		}
		bytesRead = read(fd, env, 4096);
		if (bytesRead == -1) {
			report_error(path, errno);
			continue;
		}
		env[bytesRead] = '\0';
		close(fd);
		path[len] = '\0';

		char *envp[4096];
		j = 0;
		for (int i = 0; env[i] != '\0'; j++) {
			envp[j] = env + i;
			i += strlen(env + i) + 1;
		}
		envp[j] = NULL;
		path[len] = '\0';

		report_process(pid, exe, argv, envp);
	}
	closedir(pDir);

}
