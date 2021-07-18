#ifndef DIRENTRY_H
#define DIRENTRY_H

#include <fts.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <regex.h>

typedef struct {
	char *path;
	time_t time;
} direntry;

typedef struct {
	size_t cap;
	size_t len;
	direntry *entries;
} dirlist;

dirlist *dirlist_get(const char *dirpath, const char *regex_str, int allow_dirs);
void dirlist_free(dirlist *dl);

int direntry_comp(const void *a, const void *b);

#endif
