/* See LICENSE for license details. */
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "util.h"

#define ERROR_BUF_LEN 8192

static uchar utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static Rune utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static Rune utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

/* Globals */
char errorbuf[ERROR_BUF_LEN];

/* I previously had an overly complex unrolled version of this. */
/* For simplicity and since modern compilers can optimize vector operations better than I can,
   it's probably best if I leave it to them. */
size_t
memctchr(const char *s, int c, size_t n)
{
	assert2(!STUPIDLY_BIG(n), c == (c & 255));
	size_t count = 0;
	const char *end = s + n;
	for (; s < end; n--, s++)
		if (*s == c) count++;
	return count;
}

void *
grow(void *buf, size_t *len, size_t newlen, size_t entrysize)
{
	assert6(buf, len, !buf == !*len, !STUPIDLY_BIG(*len), !STUPIDLY_BIG(newlen), entrysize < 65536);
	if (newlen > *len) {
		*len = MAX(*len*2, newlen);
		char *newbuf;
		newbuf = urealloc(buf, *len * entrysize);
		if (!newbuf) {
			fail();
			fprintf(stderr, "could noturealloc\n");
			exit(1);
		}
		return newbuf;
	}
	return buf;
}

void
userwarning(const char *s, ...)
{
	va_list va;
	va_start(va, s);
	vfprintf(stderr, s, va);
	va_end(va); 
	fail();
}

void
printsyserror(const char *str, ...)
{
	va_list ap;
	va_start(ap, str);
	vsnprintf(errorbuf, ERROR_BUF_LEN, str, ap);
	va_end(ap);
	errorbuf[ERROR_BUF_LEN-1] = '\0';
	perror(errorbuf);
}


size_t
utf8validate(Rune *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;

	return i;
}

Rune
utf8decodebyte(char c, size_t *i)
{
	for (*i = 0; *i < LEN(utfmask); ++(*i))
		if (((uchar)c & utfmask[*i]) == utfbyte[*i])
			return (uchar)c & ~utfmask[*i];

	return 0;
}

char
utf8encodebyte(Rune u, size_t i)
{
	return utfbyte[i] | (u & ~utfmask[i]);
}

size_t
utf8decode(const char *c, Rune *u, size_t clen)
{
	size_t i, j, len, type;
	Rune udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type != 0)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

size_t
utf8encode(Rune u, char *c)
{
	size_t len, i;

	len = utf8validate(&u, 0);
	if (len > UTF_SIZ)
		return 0;

	for (i = len - 1; i != 0; --i) {
		c[i] = utf8encodebyte(u, 0);
		u >>= 6;
	}
	c[0] = utf8encodebyte(u, len);

	return len;
}

void *
umalloc(size_t len)
{
	void *p;

	if (!(p = malloc(len)))
		udie("malloc: %s\n", strerror(errno));

	return p;
}

void *
urealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		udie("realloc: %s\n", strerror(errno));

	return p;
}

char *
ustrdup(char *s)
{
	if ((s = strdup(s)) == NULL)
		udie("strdup: %s\n", strerror(errno));

	return s;
}

void
udie(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}
