/* SPDX-License-Identifier: ISC */
#ifndef PAK_H
#define PAK_H
#include <stddef.h>

struct pak {
	unsigned int size;
	void *data;
};

struct pak_file {
	const char *name;
	unsigned int index;
	unsigned int size;
	const void *data;
};

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

#ifndef PAK_NO_STDIO
#include <stdio.h>
#include <stdlib.h>
int pak_read(const char *filename, struct pak *pak);
int pak_from_file(FILE *file, struct pak *pak);
#endif

int pak_from_memory(unsigned int size, void *data, struct pak *pak);

struct pak_file *pak_file_at(const struct pak *pak, int index, struct pak_file *file);
struct pak_file *pak_find_file(const struct pak *pak, const char *name, struct pak_file *file);

#define pak_for_each_file(p, f) for ((f)->index = 0; pak_file_at(p, (f)->index, f); (f)->index++)
#endif /* PAK_H */

#ifdef PAK_IMPLEMENTATION
#include <stdint.h>
#include <string.h>

static int pak_err(const char *msg)
{
#ifndef PAK_NO_STDIO
	fprintf(stderr, "%s\n", msg);
#endif
	return -1;
}

struct pak_file *pak_file_at(const struct pak *pak, int index, struct pak_file *file)
{
	const struct pak_head *hdr = pak->data;
	const struct pak_item *it = pak->data + hdr->off;

	if (index < 0 || (index * sizeof(*it)) >= hdr->len)
		return NULL;
	file->name = (const char *)&it[index].name;
	file->size = it[index].len;
	file->data = pak->data + it[index].off;
	return file;
}

struct pak_file *pak_find_file(const struct pak *pak, const char *name, struct pak_file *file)
{
	struct pak_file i = {};
	pak_for_each_file(pak, &i) {
		if (strcmp(i.name, name) == 0) {
			*file = i;
			return file;
		}
	}

	return NULL;
}

int pak_check_header(struct pak pak)
{
	const struct pak_head *hdr = pak.data;

	if (pak.size < sizeof(hdr))
		return pak_err("no header");

	if (memcmp("PACK", hdr->magic, sizeof(hdr->magic)) != 0)
		return pak_err("bad magic");

	if (pak.size < (hdr->off + hdr->len))
		return pak_err("not enough data");

	return 0;
}

int pak_from_memory(unsigned int size, void *data, struct pak *pak)
{
	struct pak p = {
		.size = size,
		.data = data,
	};

	if (pak_check_header(p))
		return -1;

	*pak = p;
	return 0;
}

#ifndef PAK_NO_STDIO

int pak_from_file(FILE *f, struct pak *pak)
{
	struct pak p = {};
	int size;
	size_t n;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size < 0)
		return pak_err("ftell");
	fseek(f, 0, SEEK_SET);

	p.size = size;
	p.data = malloc(p.size);
	if (!p.data)
		return pak_err("outofmem");

	n = fread(p.data, p.size, 1, f);
	fclose(f);
	if (n != 1) {
		free(p.data);
		return pak_err("fread");
	}

	if (pak_check_header(p)) {
		free(p.data);
		return -1;
	}
	*pak = p;
	return 0;
}

int pak_read(const char *name, struct pak *pak)
{
	FILE *f;
	int ret;

	f = fopen(name, "rb");
	if (!f)
		return pak_err("fopen");

	ret = pak_from_file(f, pak);
	fclose(f);

	return ret;
}

#endif /* PAK_NO_STDIO */
#endif /* PAK_IMPLEMENTATION */
