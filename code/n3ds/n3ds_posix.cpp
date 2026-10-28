/* POSIX pieces newlib does not provide on the 3DS */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

extern "C" void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	(void)addr; (void)prot; (void)flags;
	void *mem = malloc(len ? len : 1);
	if (!mem) return MAP_FAILED;
	off_t old = lseek(fd, 0, SEEK_CUR);
	lseek(fd, offset, SEEK_SET);
	size_t got = 0;
	while (got < len) {
		ssize_t r = read(fd, (char *)mem + got, len - got);
		if (r <= 0) break;
		got += r;
	}
	lseek(fd, old, SEEK_SET);
	return mem;
}

extern "C" int munmap(void *addr, size_t len)
{
	(void)len;
	free(addr);
	return 0;
}

/* ------------------------------------------------------------------ glob() */
/* newlib ships glob.h but no implementation. Only the last path component may
   contain wildcards, which is all the engine needs (data directory scans). */
#include <glob.h>
#include <dirent.h>
#include <fnmatch.h>
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x10
#endif
#include <string.h>
#include <strings.h>

static int glob_cmp(const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}

extern "C" int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *pglob)
{
	(void)errfunc;
	if (!(flags & GLOB_APPEND)) {
		pglob->gl_pathc = 0;
		pglob->gl_pathv = NULL;
	}

	char dir[512];
	const char *slash = strrchr(pattern, '/');
	const char *pat;
	if (slash) {
		size_t n = slash - pattern;
		if (n >= sizeof(dir)) n = sizeof(dir) - 1;
		memcpy(dir, pattern, n);
		dir[n] = 0;
		if (n == 0) strcpy(dir, "/");
		pat = slash + 1;
	} else {
		strcpy(dir, ".");
		pat = pattern;
	}

	DIR *d = opendir(dir);
	if (!d) return GLOB_NOMATCH;

	int cap = pglob->gl_pathc + 32;
	char **v = (char **)realloc(pglob->gl_pathv, sizeof(char *) * (cap + 1));
	if (!v) { closedir(d); return GLOB_NOSPACE; }
	pglob->gl_pathv = v;

	int found = 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.' && pat[0] != '.') continue;
		/* FAT is case-insensitive, so match the same way */
		if (fnmatch(pat, e->d_name, FNM_CASEFOLD) != 0) continue;
		if (pglob->gl_pathc + 1 >= cap) {
			cap *= 2;
			v = (char **)realloc(pglob->gl_pathv, sizeof(char *) * (cap + 1));
			if (!v) break;
			pglob->gl_pathv = v;
		}
		size_t len = strlen(e->d_name) + (slash ? strlen(dir) + 1 : 0) + 1;
		char *p = (char *)malloc(len);
		if (!p) break;
		if (slash) {
			if (!strcmp(dir, "/")) snprintf(p, len, "/%s", e->d_name);
			else snprintf(p, len, "%s/%s", dir, e->d_name);
		} else {
			strcpy(p, e->d_name);
		}
		pglob->gl_pathv[pglob->gl_pathc++] = p;
		found++;
	}
	closedir(d);
	pglob->gl_pathv[pglob->gl_pathc] = NULL;

	if (!(flags & GLOB_NOSORT) && pglob->gl_pathc > 1)
		qsort(pglob->gl_pathv, pglob->gl_pathc, sizeof(char *), glob_cmp);

	return found ? 0 : GLOB_NOMATCH;
}

extern "C" void globfree(glob_t *pglob)
{
	if (!pglob || !pglob->gl_pathv) return;
	for (int i = 0; i < pglob->gl_pathc; i++) free(pglob->gl_pathv[i]);
	free(pglob->gl_pathv);
	pglob->gl_pathv = NULL;
	pglob->gl_pathc = 0;
}

/* ------------------------------------------------------------------ directory cache */
/* The engine probes the SD card for loose files before every archive lookup, and a
   failed fopen() on the 3DS costs a full file-system round trip. Directory listings are
   cached in memory (case-insensitive, like FAT) and dropped whenever the game writes. */
#include <vector>
#include <string>
#include <algorithm>
#include <ctype.h>

struct n3ds_dir_entry {
	std::string dir;                 /* lowercased directory path */
	std::vector<std::string> names;  /* lowercased, sorted */
	bool exists;
};

static std::vector<n3ds_dir_entry> n3ds_dirs;

static std::string n3ds_lower(const std::string &s)
{
	std::string r(s);
	for (size_t i = 0; i < r.size(); i++) r[i] = (char)tolower((unsigned char)r[i]);
	return r;
}

extern "C" void n3ds_dircache_invalidate(void)
{
	n3ds_dirs.clear();
}

static n3ds_dir_entry &n3ds_dir_lookup(const std::string &dir)
{
	std::string key = n3ds_lower(dir);
	for (size_t i = 0; i < n3ds_dirs.size(); i++)
		if (n3ds_dirs[i].dir == key) return n3ds_dirs[i];

	n3ds_dir_entry e;
	e.dir = key;
	e.exists = false;
	DIR *d = opendir(dir.empty() ? "." : dir.c_str());
	if (d) {
		e.exists = true;
		struct dirent *de;
		while ((de = readdir(d)) != NULL) e.names.push_back(n3ds_lower(de->d_name));
		closedir(d);
		std::sort(e.names.begin(), e.names.end());
	}
	n3ds_dirs.push_back(e);
	return n3ds_dirs.back();
}

extern "C" int n3ds_file_exists(const char *path)
{
	if (!path || !*path) return 0;
	std::string p(path);
	size_t slash = p.find_last_of('/');
	std::string dir = (slash == std::string::npos) ? std::string() : p.substr(0, slash);
	std::string name = n3ds_lower((slash == std::string::npos) ? p : p.substr(slash + 1));
	if (name.empty()) return 0;
	/* wildcard names are not cached; let the caller try */
	if (name.find_first_of("*?") != std::string::npos) return 1;
	n3ds_dir_entry &e = n3ds_dir_lookup(dir);
	if (!e.exists) return 0;
	return std::binary_search(e.names.begin(), e.names.end(), name) ? 1 : 0;
}
