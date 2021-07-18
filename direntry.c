#include "direntry.h"

int direntry_comp(const void *a, const void *b) {
	const direntry *da = a;
	const direntry *db = b;
	if (da->time > db->time)
		return -1;
	else if (da->time < db->time)
		return 1;
	else
		return 0;
}

dirlist* dirlist_get(const char *dirpath, const char *regex_str, int allow_dirs) {
	dirlist *dl = calloc(1, sizeof(*dl));
	if (!dl)
		return NULL;
	dl->cap = 4;
	dl->len = 0;
	dl->entries = calloc(dl->cap, sizeof(*(dl->entries)));
	char *const paths[] = { (char *)dirpath, NULL };
	FTS *fts = fts_open(paths, FTS_LOGICAL, NULL);
	if (!fts) {
		perror("fts_open");
		return dl;
	}

	regex_t reg;
	if (regcomp(&reg, regex_str, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
		perror("regcomp");
		return dl;
	}

	for (;;) {
		FTSENT *ent = fts_read(fts);
		if (!ent) {
			if (errno == 0) {
				break;
			} else {
				perror("fts_read");
				return dl;
			}
		}
		/*
		   don't visit directories twice
		   skip preorder visit (before children)
		*/
		if (ent->fts_info == FTS_D) {
			continue;
		}
		if (!allow_dirs && (ent->fts_info == FTS_DP)) {
			continue;
		}
		if ((ent->fts_info == FTS_F) &&
		    regexec(&reg, ent->fts_name, 0, NULL, 0)) {
			continue;
		}
		if (dl->cap == dl->len) {
			dl->cap = dl->cap * 3 / 2;
			/* TODO */
			dl->entries = realloc(dl->entries, dl->cap * sizeof(*(dl->entries)));
		}
		dl->entries[dl->len].path = strdup(ent->fts_path);
		dl->entries[dl->len].time = ent->fts_statp->st_ctime;
		dl->len++;
	}
	regfree(&reg);
	fts_close(fts);
	return dl;
}

void dirlist_free(dirlist *dl) {
	if (!dl)
		return;
	for(size_t i = 0; i < dl->len; ++i)
		free(dl->entries[i].path);
	free(dl->entries);
	free(dl);
}
