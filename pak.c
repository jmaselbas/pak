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
	// TODO: handle "writer" mode
}

static void
pak_create(struct pak *pak, FILE *f)
{
	struct pak_head hdr = { 0 };
	struct pak p = {
		.file = f,
		.size = sizeof(hdr),
	};

	/* when file is given the archive will be written on the fly, file
	 * content will not be kep in memory, the archive is "write-only" */
	// TODO: test if f is seekable ie ESPIPE
	if (p.file) {
		/* write a blank header, this will be overwritten later */
		fseek(f, 0, SEEK_SET);
		fwrite(&hdr, sizeof(hdr), 1, f);
		p.size += sizeof(hdr);
	}
	*pak = p;
}

static int pak_push_pak_file(struct pak *pak, const struct pak_file *pf)
{
	struct pak_item itm = {
		.off = pak->size,
		.len = pf->size,
	};
	void *p;
	int n;

	memcpy(itm.name, pf->name, strnlen(pf->name, sizeof(itm.name) - 1));
	if (pak->file) { // TODO: use a proper flag
		n = fwrite(pf->data, pf->size, 1, pak->file);
		if (n != 1)
			return pak_err("fwrite");
	} else {
		p = realloc(pak->data, pak->size + pf->size);
		if (!p)
			return pak_err("realloc");
		pak->data = p;
		memcpy(pak->data + pak->size, pf->data, pf->size);
	}
	pak->size += pf->size;

	/* now push the file item */
	p = realloc(pak->items, (pak->count + 1) * sizeof(struct pak_item));
	if (!p)
		return pak_err("realloc");
	pak->items = p;
	pak->items[pak->count++] = itm;
	return 0;
}

static int pak_push_file(struct pak *pak, const char *name, size_t size, const void *data)
{
	struct pak_file pf = {
		.name = name,
		.size = size,
		.data = data,
	};
	return pak_push_pak_file(pak, &pf);
}

static int pak_push_from_file(struct pak *pak, const char *name, FILE *f)
{
	char *p, *buf = NULL;
	size_t n, len = 0;
	int ret;
	do {
		p = realloc(buf, len + 4096);
		if (!p) {
			free(buf);
			return pak_err("realloc");
		}
		buf = p;
		n = fread(buf + len, sizeof(char), 4096, f);
		len += n;
		if (n < 4096)
			break;
	} while (!feof(f));
	ret = pak_push_file(pak, name, len, buf);
	free(buf);
	return ret;
}

static int pak_push_from_path(struct pak *pak, const char *path)
{
	FILE *f = fopen(path, "rb");
	int ret;
	if (!f)
		return pak_err("fopen");
	// TODO: test for S_ISDIR(stat.st_mode)
	ret = pak_push_from_file(pak, path, f);
	fclose(f);
	return ret;
}

static int pak_write_file(struct pak *pak, FILE *f)
{
	struct pak_head hdr = {
		.magic = "PACK",
		.off = pak->size, /* end of file */
		.len = pak->count * sizeof(struct pak_item),
	};

	if (pak->file && f != pak->file)
		return pak_err("file mismatch");
	if (f == NULL)
		f = pak->file;
	if (f == NULL)
		return pak_err("no file");

	if (!pak->file) { // TODO: use a proper flag
		memcpy(pak->data, &hdr, sizeof(hdr));
		fwrite(pak->data, pak->size, 1, f);
		fwrite(pak->items, pak->count, sizeof(struct pak_item), f);
	} else {
		fwrite(pak->items, pak->count, sizeof(struct pak_item), f);
		/* now update the header */
		fseek(f, 0, SEEK_SET);
		fwrite(&hdr, sizeof(hdr), 1, f);
	}

	return 0;
}

static void
pak_files(const char *name, int count, char **file)
{

	struct pak pak;
	FILE *f;
	int i, r;

	mkdir_parent(name);
	f = fopen(name, "wb");
	if (!f) {
		perror("fopen");
		exit(1);
	}
	pak_create(&pak, NULL);
	for (i = 0; i < count; i++) {
		if (pak_push_from_path(&pak, file[i]))
			break;
	}
	pak_write_file(&pak, f);
	pak_free(&pak);
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
