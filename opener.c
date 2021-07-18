#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <libgen.h>
#include "direntry.h"

static int get_term_lines() {
	struct winsize size;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1) {
		perror("ioctl");
		return -1;
	}
	return size.ws_row;
}

static void usage() {
	fprintf(stderr, "bad usage\n");
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
	FILE *output = fdopen(fd, "r");
	char *line = NULL;
	size_t nread = 0;
	size_t len = 0;
	if ((nread = getline(&line, &len, output)) == -1UL) {
		fclose(output);
		return NULL;
	}
	if (line[nread - 1] == '\n')
		line[nread - 1] = '\0';
	fclose(output);
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
		char *const args[] = { "alacritty", "-e", "sh", "-c", str, NULL };
		execvp(args[0], args);
	} else if (exec_pid == -1) {
		/* TODO */
	}
	close(pre_pipe[0]);
	close(post_pipe[1]);

	npipe[0] = post_pipe[0];
	npipe[1] = pre_pipe[1];
	return 0;
}

static char *pick_path(const char *dirpath, const char *regex_str, int lines, int allow_dirs, const char *program) {
	char *fullpath = NULL;
	dirlist *dl = dirlist_get(dirpath, regex_str, allow_dirs);
	qsort(dl->entries, dl->len, sizeof(*(dl->entries)), direntry_comp);
	int pipe[2];
	char *prompt = alloc_sprintf("%s ", program);
	if (selector_pipe(pipe, lines, prompt) == -1)
		goto error_dl;
	pid_t write_pid = fork();
	if (write_pid == -1)
		goto error_dl;
	if (write_pid == 0) {
		close(pipe[0]);
		for(size_t i = 0; i < dl->len; ++i) {
			const char *path = dl->entries[i].path;
			size_t len = strlen(path);
			/* int count = 0; */
			/* size_t last = len; */
			/* for(;last <= len; --last) { */
			/* 	if (path[last] == '/') */
			/* 		count++; */
			/* 	if (count == 2) */
			/* 		break; */
			/* } */
			/* last++; */
			/* dprintf(pipe[1], "%s\n", path + last); */
			size_t slash_count = 0;
			size_t i = 0;
			/* /home/mat/videos/ = 4 slashes */
			const size_t cut = 4;
			for (; i < len && slash_count < cut; ++i) {
				if (path[i] == '/')
					slash_count++;
			}
			if (slash_count == cut)
				dprintf(pipe[1], "%s\n", path + i);
		}
		exit(0);
	}
	close(pipe[1]);

	char *line = read_line_fd(pipe[0]);
	close(pipe[0]);
	if (!line)
		goto error_dl;
	size_t line_len = strlen(line);
	size_t match = 0;
	for (size_t i = 0; i < dl->len; ++i) {
		size_t len = strlen(dl->entries[i].path);
		size_t j = 0;
		for (; j < line_len; ++j) {
			if(line[j] != dl->entries[i].path[len - line_len + j])
				break;
		}
		if(j == line_len) {
			match = i;
			break;
		}
	}
	fullpath = strdup(dl->entries[match].path);
	free(line);
error_dl:
	free(prompt);
	dirlist_free(dl);
	return fullpath;
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

	int exit_code = EXIT_SUCCESS;
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
		const char *const args[] = { program, fullpath, NULL };
		execvp(args[0], (char *const *)args);
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
