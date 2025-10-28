#include <ctype.h>
#include <solution.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#define ARGV_MAX 4096
#define ENV_MAX 4096


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
		char *strPid = pDirent->d_name;

		int isPid = 1;
		for (int i = 0; strPid[i] != '\0'; i++) {
			if (!isdigit(strPid[i])) {
				isPid = 0;
				break;
			}
		}
		if (!isPid) continue;

		int intPid = atoi(strPid);
		char path[PATH_MAX];


		snprintf(path, PATH_MAX, "/proc/%s/exe", strPid);
		char exe[PATH_MAX];
		if (realpath(path, exe) == NULL) {
			report_error(path, errno);
			continue;
		}

		snprintf(path, PATH_MAX, "/proc/%s/cmdline", strPid);
		char cmdline[ARGV_MAX];
		if ((fd = open(path, O_RDONLY)) == -1) {
			report_error(path, errno);
			continue;
		}
		int bytesRead = read(fd, cmdline, ARGV_MAX);
		close(fd);
		if (bytesRead == -1) {
			report_error(path, errno);
			continue;
		}
		cmdline[bytesRead] = '\0';
		char *argv[ARGV_MAX];
		int j = 0;
		for (int i = 0; i < ARGV_MAX && j < ARGV_MAX - 1 && cmdline[i] != '\0'; j++) {
			argv[j] = cmdline + i;
			i += strnlen(cmdline + i, ARGV_MAX - i) + 1;
		}
		argv[j] = NULL;



		snprintf(path, PATH_MAX, "/proc/%s/environ", strPid);
		char env[ENV_MAX];
		if ((fd = open(path, O_RDONLY)) == -1) {
			report_error(path, errno);
			continue;
		}
		bytesRead = read(fd, env, 4096);
		close(fd);
		if (bytesRead == -1) {
			report_error(path, errno);
			continue;
		}
		env[bytesRead] = '\0';

		char *envp[ENV_MAX];
		j = 0;
		for (int i = 0; i < ENV_MAX && j < ENV_MAX - 1 && env[i] != '\0'; j++) {
			envp[j] = env + i;
			i += strnlen(env + i, ENV_MAX - i) + 1;
		}
		envp[j] = NULL;

		report_process(intPid, exe, argv, envp);
	}
	closedir(pDir);

}
