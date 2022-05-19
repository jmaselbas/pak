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

struct pak_head {
	char magic[4];
	uint32_t off;
	uint32_t len;
};

struct pak_item {
	char name[56];
	uint32_t off;
	uint32_t len;
};

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
write_file(const char *name, const char *buf, size_t size)
{
	int ret = 0;
	FILE *f = fopen(name, "w");

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

static void
unpak(const char *buf, size_t size)
{
	struct pak_head hdr;
	struct pak_item *itm;
	size_t i;

	if (size < sizeof(hdr)) {
		fprintf(stderr, "not enough data: %d\n", size);
		exit(1);
	}

	memcpy(&hdr, buf, sizeof(hdr));

	if (strncmp("PACK", hdr.magic, sizeof(hdr.magic)) != 0) {
		fprintf(stderr, "bad magic\n");
		exit(1);
	}

	if (size < (hdr.off + hdr.len)) {
		fprintf(stderr, "not enough data: %d+%d vs %d\n", hdr.off, hdr.len, size);
		exit(1);
	}

	itm = (struct pak_item *)(buf + hdr.off);
	for (i = 0; i < (hdr.len / sizeof(*itm)); i++) {
		if (list)
			printf("%s\n", itm[i].name);

		if (extract) {
			mkdir_parent(itm[i].name);
			write_file(itm[i].name, buf + itm[i].off, itm[i].len);
		}
	}
}

static void
unpak_file(const char *name)
{
	struct stat sb;
	void *buf;
	int fd;

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
	if (buf == NULL) {
		perror("mmap");
		exit(1);
	}
	unpak(buf, sb.st_size);
	munmap(buf, sb.st_size);
}

static size_t
fwrite_file(const char *name, FILE *f)
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
	if (buf == NULL) {
		perror("mmap");
		exit(1);
	}
	n = fwrite(buf, 1, sb.st_size, f);
	munmap(buf, sb.st_size);

	return n;
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
	f = fopen(name, "w");
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
		itm[i].len = fwrite_file(itm[i].name, f);
	}

	off = ftell(f);
	if (off >= (1ULL << 32)) {
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
	fprintf(stderr, "usage: pak [-xt] FILE [FILE ...]\n");
	fprintf(stderr, "       unpak [-xt] FILE\n");
	exit(1);
}

int
main(int argc, char **argv)
{
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
		file = EARGF(usage);
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

	if (extract || list)
		unpak_file(file);
	else if (argc > 0)
		pak_files(file, argc, argv);

	return 0;
}
