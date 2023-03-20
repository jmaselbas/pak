/* SPDX-License-Identifier: ISC */
/* SPDX-FileCopyrightText: 2022 Jules Maselbas */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <stdint.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include "arg.h"
#define PAK_IMPLEMENTATION
#include "pak.h"

#define __noreturn __attribute__((noreturn))
__noreturn static void usage(void);

static int list;
static int extract;

static void
mkdir_parent(const char *filepath)
{
	char *path = strdup(filepath);
	char *file = basename(path);
	char *d;
	char *p;

	do {
		p = dirname(path);
	} while (*p != '.' && *p != '/');

	d = path;
	while (d < file) {
		if (mkdir(path, 0755) == -1) {
			if (errno != EEXIST)
				perror(d);
		}
		d += strlen(d);
		*d = '/';
		d++;
	}

	free(path);
}

static int
write_file(const char *name, size_t size, const char *buf)
{
	int ret = 0;
	FILE *f = fopen(name, "wb");

	if (!f) {
		perror(name);
		return -1;
	}

	if (fwrite(buf, size, 1, f) != 1) {
		perror(name);
		ret = -1;
	}

	fclose(f);

	return ret;
}

static size_t
fwrite_file(FILE *f, const char *name)
{
	struct stat sb;
	void *buf;
	int fd;
	size_t n;

	fd = open(name, O_RDONLY);
	if (fd == -1) {
		perror(name);
		exit(1);
	}
	if (fstat(fd, &sb) < 0) {
		perror("fstat");
		exit(1);
	}
	buf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (buf == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	n = fwrite(buf, 1, sb.st_size, f);
	munmap(buf, sb.st_size);

	return n;
}

static void
do_unpak(struct pak *pak)
{
	struct pak_file f;

	pak_for_each_file(pak, &f) {
		if (list)
			printf("%s\n", f.name);

		if (extract) {
			mkdir_parent(f.name);
			write_file(f.name, f.size, f.data);
		}
	}
}

static int pak_read_mmap(const char *name, struct pak *pak)
{
	struct pak p = {
		.flag = PAK_FROM_MMAP,
	};
	struct stat sb;
	int fd;

	fd = open(name, O_RDONLY);
	if (fd == -1)
		return pak_err("open");
	if (fstat(fd, &sb) < 0)
		return pak_err("fstat");
	p.size = sb.st_size;
	p.data = mmap(NULL, p.size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (p.data == MAP_FAILED)
		return pak_err("mmap");

	if (pak_check_header(p)) {
		munmap(p.data, p.size);
		return -1;
	}

	*pak = p;
	return 0;
}

static void pak_free(struct pak *pak)
{
	if (pak->flag == PAK_FROM_FILE)
		free(pak->data), pak->data = NULL;
	if (pak->flag == PAK_FROM_MMAP)
		munmap(pak->data, pak->size), pak->data = NULL;
}

static void
pak_files(const char *name, int count, char **file)
{
	struct pak_head hdr = { 0 };
	struct pak_item *itm = NULL;
	size_t n;
	long off;
	int i;
	FILE *f;

	mkdir_parent(name);
	f = fopen(name, "wb");
	if (!f) {
		perror("fopen");
		exit(1);
	}
	/* write a blank header, this will be overwritten later */
	fwrite(&hdr, sizeof(hdr), 1, f);

	itm = calloc(count, sizeof(*itm));
	if (!itm) {
		perror("malloc");
		exit(1);
	}

	for (i = 0; i < count; i++) {
		n = snprintf(itm[i].name, sizeof(itm[i].name), "%s", file[i]);
		if (n == sizeof(itm[i].name)) {
			fprintf(stderr, "%s: filename too long\n", file[i]);
			exit(1);
		}
		itm[i].off = ftell(f);
		itm[i].len = fwrite_file(f, itm[i].name);
	}

	off = ftell(f);
	if ((unsigned long long)off >= (1ULL << 32)) {
		fprintf(stderr, "cannot create %s: too much data\n", name);
		exit(1);
	}

	fwrite(itm, sizeof(*itm), count, f);
	free(itm);

	/* now update the header */
	memcpy(hdr.magic, "PACK", 4);
	hdr.off = off;
	hdr.len = count * sizeof(*itm);
	fseek(f, 0, SEEK_SET);
	fwrite(&hdr, sizeof(hdr), 1, f);
}

char *argv0;

static void
usage(void)
{
	fprintf(stderr, "usage: pak [-xt] [-f] ARCHIVE [FILE ...]\n");
	fprintf(stderr, "       unpak [-xt] [-f] ARCHIVE\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct pak pak;
	char *name;
	char *file = NULL;

	name = strdup(argv[0]);
	if (name && strcmp(basename(name), "unpak") == 0)
		extract = 1;
	free(name);

	ARGBEGIN {
	case 'x':
		extract = 1;
		break;
	case 't':
		list = 1;
		break;
	case 'f':
		file = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1)
		usage();

	if (file == NULL) {
		file = argv[0];
		argc--;
		argv++;
	}

	if (extract || list) {
		if (pak_read_mmap(file, &pak))
			return -1;
		do_unpak(&pak);
		pak_free(&pak);
	} else if (argc > 0) {
		pak_files(file, argc, argv);
	}

	return 0;
}
