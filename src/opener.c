#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <libgen.h>
#include <regex.h>
#include <fts.h>
#include <errno.h>
#include <string.h>

static int get_term_lines() {
	struct winsize size;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1) {
		perror("ioctl");
		return -1;
	}
	return size.ws_row;
}

static void usage() {
	fprintf(stderr, "opener [-r] [-d] program regex path\n");
}

static char *alloc_sprintf(const char *fmt, ...) {
	int n = 0;
	size_t size = 0;
	char *p = NULL;
	va_list ap;

	/* Determine required size. */

	va_start(ap, fmt);
	n = vsnprintf(p, size, fmt, ap);
	va_end(ap);

	if (n < 0)
	    return NULL;

	size = (size_t) n + 1;      /* One extra byte for '\0' */
	p = malloc(size);
	if (p == NULL)
	    return NULL;

	va_start(ap, fmt);
	n = vsnprintf(p, size, fmt, ap);
	va_end(ap);

	if (n < 0) {
	    free(p);
	    return NULL;
	}

	return p;
}

static char *read_line_fd(int fd) {
	char *line = NULL;
	size_t cap = 32;
	size_t len = 0;
	while ((line = realloc(line, cap))) {
		size_t left = cap - len;
		if (!left) {
			cap *= 2;
			continue;
		}
		ssize_t r = read(fd, line + len, left);
		if (r < 0) {
			free(line);
			return NULL;
		} else if (r == 0) {
			break;
		} else {
			len += (size_t)r;
		}
	}
	if (!line)
		return NULL;
	if (!len) {
		free(line);
		return NULL;
	}
	line[len - 1] = '\0'; /* replace newline */
	return line;
}

static int selector_pipe(int npipe[2], int lines, const char *prompt) {
	if (!prompt)
		prompt = "> ";
	int pre_pipe[2];
	if (pipe(pre_pipe) == -1)
		return -1;
	int post_pipe[2];
	if (pipe(post_pipe) == -1)
		return -1;
	pid_t exec_pid = fork();
	if (exec_pid == 0) {
		close(pre_pipe[1]);
		close(post_pipe[0]);
		const char *fmt = "fzy -p '%s' -l %d 0>&%d 1>&%d";
		char *str = alloc_sprintf(fmt, prompt, lines, pre_pipe[0], post_pipe[1]);
		/* const char *fmt = "fzf --reverse --prompt='%s' 0>&%d 1>&%d"; */
		/* char *str = alloc_sprintf(fmt, prompt, pre_pipe[0], post_pipe[1]); */
		execlp("alacritty", "alacritty", "-e", "sh", "-c", str, (char *)NULL );
	} else if (exec_pid == -1) {
		close(pre_pipe[1]);
		close(post_pipe[0]);
	}
	close(pre_pipe[0]);
	close(post_pipe[1]);

	npipe[0] = post_pipe[0];
	npipe[1] = pre_pipe[1];
	return 0;
}

static int fts_strcmp_path(const FTSENT **a, const FTSENT **b) {
	return strcmp((*a)->fts_path, (*b)->fts_path);
}

static void write_files(int fd, char *dirpath, const char *regex_str, int allow_dirs) {
	regex_t reg;
	if (regcomp(&reg, regex_str, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
		perror("regcomp");
		return;
	}
	FTS *fts = fts_open((char *[]){dirpath, NULL} , FTS_LOGICAL, fts_strcmp_path);
	if (!fts) {
		perror("fts");
		goto fail_fts;
	}
	for (;;) {
		FTSENT *ent = fts_read(fts);
		if (!ent) {
			if (errno == 0) {
				break;
			} else {
				perror("fts_read");
				goto fail_fts_read;
			}
		}
		if (ent->fts_info == FTS_DP) {
			continue;
		}
		if (!allow_dirs && ent->fts_info == FTS_D) {
			continue;
		}
		if (ent->fts_info == FTS_F &&
		    regexec(&reg, ent->fts_name, 0, NULL, 0)) {
			continue;
		}
		char *cut = ent->fts_path;
		for (int i = 0; i < 4; ++i)
			cut = strchr(cut, '/') + 1;
		write(fd, cut, strlen(cut));
		write(fd, "\n", 1);
	}
fail_fts_read:
	fts_close(fts);
fail_fts:
	regfree(&reg);
}

static char *pick_path(char *dirpath, const char *regex_str, int lines, int allow_dirs, const char *program) {
	char *line = NULL;
	int pipe[2];
	char *prompt = alloc_sprintf("%s ", program);
	if (selector_pipe(pipe, lines, prompt) == -1)
		goto fail;
	pid_t write_pid = fork();
	if (write_pid == -1)
		goto fail;
	if (write_pid == 0) {
		close(pipe[0]);
		write_files(pipe[1], dirpath, regex_str, allow_dirs);
		exit(0);
	}
	close(pipe[1]);

	char *name = read_line_fd(pipe[0]);
	close(pipe[0]);
	if (!name)
		goto fail;
	size_t dirpath_len = strlen(dirpath);
	size_t name_len = strlen(name);
	line = malloc(dirpath_len + name_len + 2);
	memcpy(line, dirpath, dirpath_len);
	memcpy(line + dirpath_len, "/", 1);
	memcpy(line + dirpath_len + 1, name, name_len);
	memcpy(line + dirpath_len + 1 + name_len, "\0", 1);
	free(name);
fail:
	free(prompt);
	return line;
}

static int pick_yes_no(const char *prompt, int lines) {
	int response = 0;
	int pipe[2];
	if (selector_pipe(pipe, lines, prompt) == -1)
		return response;
	pid_t write_pid = fork();
	if (write_pid == -1)
		return response;
	if (write_pid == 0) {
		close(pipe[0]);
		dprintf(pipe[1], "No\nYes\n");
		exit(0);
	}
	close(pipe[1]);
	char *response_line = read_line_fd(pipe[0]);
	close(pipe[0]);
	if (!response_line)
		return response;
	if (!strcmp(response_line, "Yes"))
		response = 1;
	free(response_line);
	return response;
}

int main(int argc, char *argv[]) {
	int lines = get_term_lines();
	if (lines == -1) {
		lines = 40;
	}

	int opt;
	int remove = 0;
	int allow_dirs = 0;
	while ((opt = getopt(argc, argv, "rd")) != -1) {
		switch (opt) {
		case 'r':
			remove = 1;
			break;
		case 'd':
			allow_dirs = 1;
			break;
		default:
			usage();
			return -1;
		}
	}

	if (optind + 2 >= argc) {
		usage();
		return -1;
	}

	char *program = argv[optind++];
	char *regex_str = argv[optind++];
	char *dirpath = argv[optind++];
	char *fullpath = pick_path(dirpath, regex_str, lines, allow_dirs, program);
	if (!fullpath)
		return -1; /* TODO */

	pid_t program_pid = fork();
	if (program_pid == 0) {
		execlp(program, program, fullpath, (char *)NULL);
	} else if (program_pid == -1) {
		/* TODO */
	}

	waitpid(program_pid, NULL, 0);

	if (remove) {
		int response = pick_yes_no("Remove? ", lines);
		if (response) {
			unlink(fullpath);
			char *dir_orig = strdup(fullpath);
			char *dir = dirname(dir_orig);
			int old_errno = errno;
			while (rmdir(dir) != -1)
				dir = dirname(dir);
			errno = old_errno;
			free(dir_orig);
		}
	}

	free(fullpath);
}
