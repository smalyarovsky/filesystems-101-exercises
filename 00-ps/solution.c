#include <ctype.h>
#include <solution.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#define ARGV_MIN 4096

char ** splitToStrings(char *buffer, const size_t size) {
	char **result = malloc((size / 2 + 1) * sizeof(char *));
	if (!result) {
		perror("malloc");
		return NULL;
	}
	size_t j = 0;
	for (size_t i = 0; i < size && buffer[i] != '\0'; i += strnlen(buffer + i, size - i - 1) + 1) {
		result[j++] = buffer + i;
	}
	result[j] = NULL;
	return result;
}

char * readWholeFile(const char *filename, size_t *bytes) {
	int fd = open(filename, O_RDONLY);
	if (fd == -1) {
		report_error(filename, errno);
		return NULL;
	}

	size_t size = 4096;
	char *buffer = malloc(size);
	if (!buffer) {
		perror("malloc");
		close(fd);
		return NULL;
	}

	size_t bytesRead = 0, bytesReadTotal = 0;
	while ((bytesRead = read(fd, buffer + bytesReadTotal, size - bytesReadTotal)) > 0) {
		bytesReadTotal += bytesRead;
		if (bytesReadTotal == size) {
			size *= 2;
			char *tmp = realloc(buffer, size);
			if (!tmp) {
				perror("realloc");
				free(buffer);
				close(fd);
				return NULL;
			}
			buffer = tmp;
		}
	}
	close(fd);
	*bytes = bytesReadTotal;
	buffer[bytesReadTotal] = '\0';
	return buffer;
}

void ps(void) {
	struct dirent *pDirent;
 	DIR *pDir = opendir("/proc");
	if (pDir == NULL) {
		report_error("/proc", errno);
		return;
	}

	while ((pDirent = readdir(pDir)) != NULL) {
		char *endptr;
		const pid_t pid = (pid_t) strtol(pDirent->d_name, &endptr, 10);
		if (*endptr != '\0') {
			continue;
		}


		char path[PATH_MAX];

		snprintf(path, PATH_MAX, "/proc/%s/exe", pDirent->d_name);
		char exe[PATH_MAX];
		if (realpath(path, exe) == NULL) {
			report_error(path, errno);
			continue;
		}

		size_t size;

		snprintf(path, PATH_MAX, "/proc/%s/cmdline", pDirent->d_name);
		char *cmdline = readWholeFile(path, &size);
		if (!cmdline) {
			exit(1);
		}
		char **argv = splitToStrings(cmdline, size);
		if (!argv) {
			free(cmdline);
			exit(1);
		}
		snprintf(path, PATH_MAX, "/proc/%s/environ", pDirent->d_name);
		char *env = readWholeFile(path, &size);
		if (!env) {
			free(cmdline);
			free(argv);
			exit(1);
		}
		char **envp = splitToStrings(env, size);
		if (!envp) {
			free(cmdline);
			free(argv);
			free(env);
			exit(1);
		}

		report_process(pid, exe, argv, envp);

		free(cmdline);
		free(argv);
		free(env);
		free(envp);
	}
	closedir(pDir);

}
