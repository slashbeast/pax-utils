/*
 * Copyright 2003-2007 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/pax-utils/paxinc.c,v 1.14 2010/12/08 01:16:01 vapier Exp $
 *
 * Copyright 2005-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2007 Mike Frysinger  - <vapier@gentoo.org>
 */

/* stick common symbols here that are needed by paxinc.h */

#define IN_paxinc
#include "paxinc.h"

char do_reverse_endian;

/* some of this ar code was taken from busybox */

#define AR_MAGIC "!<arch>"
#define AR_MAGIC_SIZE (sizeof(AR_MAGIC)-1) /* dont count null byte */
archive_handle *ar_open_fd(const char *filename, int fd)
{
	static archive_handle ret;
	char buf[AR_MAGIC_SIZE];

	ret.filename = filename;
	ret.fd = fd;
	ret.skip = 0;
	ret.extfn = NULL;

	if (read(ret.fd, buf, AR_MAGIC_SIZE) != AR_MAGIC_SIZE)
		return NULL;
	if (strncmp(buf, AR_MAGIC, AR_MAGIC_SIZE))
		return NULL;

	return &ret;
}
archive_handle *ar_open(const char *filename)
{
	int fd;
	archive_handle *ret;

	if ((fd=open(filename, O_RDONLY)) == -1)
		err("Could not open '%s'", filename);

	ret = ar_open_fd(filename, fd);
	if (ret == NULL)
		close(fd);

	return ret;
}

archive_member *ar_next(archive_handle *ar)
{
	char *s;
	size_t len = 0;
	static archive_member ret;

	if (ar->skip && lseek(ar->fd, ar->skip, SEEK_CUR) == -1) {
close_and_ret:
		close(ar->fd);
		return NULL;
	}

	if (read(ar->fd, ret.buf.raw, sizeof(ret.buf.raw)) != sizeof(ret.buf.raw))
		goto close_and_ret;

	/* ar header starts on an even byte (2 byte aligned)
	 * '\n' is used for padding */
	if (ret.buf.raw[0] == '\n') {
		memmove(ret.buf.raw, ret.buf.raw+1, 59);
		if (read(ar->fd, ret.buf.raw+59, 1) != 1)
			goto close_and_ret;
	}

	if ((ret.buf.formatted.magic[0] != '`') || (ret.buf.formatted.magic[1] != '\n')) {
		warn("Invalid ar entry");
		goto close_and_ret;
	}

	if (ret.buf.formatted.name[0] == '/' && ret.buf.formatted.name[1] == '/') {
		if (ar->extfn != NULL) {
			warn("Duplicate GNU extended filename section");
			goto close_and_ret;
		}
		len = atoi(ret.buf.formatted.size);
		/* we will leak this memory */
		ar->extfn = xmalloc(sizeof(char) * (len + 1));
		if (read(ar->fd, ar->extfn, len) != len)
			goto close_and_ret;
		ar->extfn[len--] = '\0';
		for (; len > 0; len--)
			if (ar->extfn[len] == '\n')
				ar->extfn[len] = '\0';
		ar->skip = 0;
		return ar_next(ar);
	}

	s = ret.buf.formatted.name;
	if (s[0] == '#' && s[1] == '1' && s[2] == '/') {
		/* BSD extended filename, always in use on Darwin */
		len = atoi(s + 3);
		if (len <= sizeof(ret.buf.formatted.name)) {
			if (read(ar->fd, ret.buf.formatted.name, len) != len)
				goto close_and_ret;
		} else {
			s = alloca(sizeof(char) * len);
			if (read(ar->fd, s, len) != len)
				goto close_and_ret;
		}
	} else if (s[0] == '/' && s[1] >= '0' && s[1] <= '9') {
		/* GNU extended filename */
		if (ar->extfn == NULL) {
			warn("GNU extended filename without special data section");
			goto close_and_ret;
		}
		s = ar->extfn + atoi(s + 1);
	}

	snprintf(ret.name, sizeof(ret.name), "%s:%s", ar->filename, s);
	if ((s=strchr(ret.name+strlen(ar->filename), '/')) != NULL)
		*s = '\0';
	ret.date = atoi(ret.buf.formatted.date);
	ret.uid = atoi(ret.buf.formatted.uid);
	ret.gid = atoi(ret.buf.formatted.gid);
	ret.mode = strtol(ret.buf.formatted.mode, NULL, 8);
	ret.size = atoi(ret.buf.formatted.size);
	ar->skip = ret.size - len;

	return &ret;
}

/* Convert file perms into octal string */
const char *strfileperms(const char *fname)
{
	struct stat st;
	static char buf[8];

	if (stat(fname, &st) == -1)
		return "";

	snprintf(buf, sizeof(buf), "%o", st.st_mode);

	return buf + 2;
}

/* Color helpers */
#define COLOR(c,b) "\e[" c ";" b "m"
const char *NORM   = COLOR("00", "00");
const char *RED    = COLOR("31", "01");
const char *YELLOW = COLOR("33", "01");

void color_init(bool disable)
{
	if (!disable) {
		const char *nocolor = getenv("NOCOLOR");
		if (nocolor)
			disable = !strcmp(nocolor, "yes") || !strcmp(nocolor, "true");
	}
	if (disable)
		NORM = RED = YELLOW = "";
}
