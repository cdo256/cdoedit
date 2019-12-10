/* See LICENSE for license details. */
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "editor.h"

#define MAX_LINE_LEN 1024
#define MAX_UTF8_BYTES 6

#define SIGN(a) ((a)>0 ? +1 : (a)<0 ? -1 : 0)
#define ISSELECT(a) ((a) == -2 || (a) == 2)
#define POSCMP(a,b) ((a).line == (b).line ? (b).pos - (a).pos : (b).line - (a).line);

#define STUPIDLY_BIG(x) ((size_t)(x) & 0xFFFF000000000000ULL)
#define assert_valid_pos(d, x) \
	assert3((d)->bufstart <= (x), (x) <= (d)->curleft || (d)->curright <= (x), (x) <= (d)->bufend)
#define assert_valid_read(d, x) \
	assert_valid_pos(d,x); assert2((x) != (d)->curleft, (x) != (d)->bufend)
#define assert_valid_write(d, x) assert2((d)->bufstart <= (x), (x) < (d)->bufend)
#define assert_valid_read_range(d, x, y) \
	assert4((x) <= (y), (d)->bufstart <= (x), (y) <= (d)->curleft || (d)->curright <= (x), (y) <= (d)->bufend)
#define assert_valid_write_range(d, x,y) assert2((d)->bufstart <= (x), (y) < (d)->bufend)

#ifdef NDEBUG
#define assert1(a1)
#define assert2(a1,a2)
#define assert3(a1,a2,a3)
#define assert4(a1,a2,a3,a4)
#define assert5(a1,a2,a3,a4,a5)
#define assert6(a1,a2,a3,a4,a5,a6)
#define assert7(a1,a2,a3,a4,a5,a6,a7)
#define assert8(a1,a2,a3,a4,a5,a6,a7,a8)
#define assert9(a1,a2,a3,a4,a5,a6,a7,a8,a9)
#define assert10(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10)
#define assert11(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11)
#define assert12(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12)
#define FAIL()
#else
#define assert1(a1) assert(a1)
#define assert2(a1,a2) assert(a1); assert1(a2)
#define assert3(a1,a2,a3) assert(a1); assert2(a2,a3)
#define assert4(a1,a2,a3,a4) assert(a1); assert3(a2,a3,a4)
#define assert5(a1,a2,a3,a4,a5) assert(a1); assert4(a2,a3,a4,a5)
#define assert6(a1,a2,a3,a4,a5,a6) assert(a1); assert5(a2,a3,a4,a5,a6)
#define assert7(a1,a2,a3,a4,a5,a6,a7) assert(a1); assert6(a2,a3,a4,a5,a6,a7)
#define assert8(a1,a2,a3,a4,a5,a6,a7,a8) assert(a1); assert7(a2,a3,a4,a5,a6,a7,a8)
#define assert9(a1,a2,a3,a4,a5,a6,a7,a8,a9) assert(a1); assert8(a2,a3,a4,a5,a6,a7,a8,a9)
#define assert10(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) assert(a1); assert9(a2,a3,a4,a5,a6,a7,a8,a9,a10)
#define assert11(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) assert(a1); assert10(a2,a3,a4,a5,a6,a7,a8,a9,a11)
#define assert12(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12) assert(a1); assert11(a2,a3,a4,a5,a6,a7,a8,a9,a12)
#define fail() assert(0)
#endif

typedef struct {
	char *bufstart;		/* buffer for storing chunks of the file */
	char *bufend;	        /* one past the end of the buffer */
	char *curleft;		/* one past the end of the upper section */
	char *curright;         /* start of the lower section */
	char *renderstart;      /* top left of the editor */
	char *selanchor;
	bool scrolldirty;
	bool coldirty;
	int col;
} Document;

typedef struct {
	int line;
	int pos;
} Pos;

/* Globals */
static Document doc;
char *scratchbuf = NULL;
size_t scratchlen = 0;

/* I previously had an overly complex unrolled version of this. */
/* For simplicity and since modern compilers can optimize vector operations better than I can,
   it's probably best if I leave it to them. */
size_t
memctchr(const char *s, int c, size_t n)
{
	assert_valid_read_range(&doc, s, s+n); /* remove line if using this outside doc */
	assert2(!STUPIDLY_BIG(n), c == (c & 255));
	size_t count = 0;
	const char *end = s + n;
	for (; s < end; n--, s++)
		if (*s == c) count++;
	return count;
}

ssize_t
grow(char **buf, size_t *len, size_t newlen)
{
	assert5(buf, len, !*buf == !*len, !STUPIDLY_BIG(*len), !STUPIDLY_BIG(newlen));
	if (newlen > *len) {
		*len = MAX(*len*2, newlen);
		char *newbuf;
		newbuf = realloc(*buf, *len);
		if (!newbuf) {
			fail();
			fprintf(stderr, "could not realloc\n");
			exit(1);
		}
		ssize_t shift = newbuf - *buf;
		*buf = newbuf;
		return shift;
	}
	return 0;
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

Rune
readchar(const char *c, /*out*/ const char **next)
{
	assert_valid_read(&doc, c);
	assert(next);
	Rune r;
	if (!(*c & 128)) {
		r = *c;
		*next = c+1;
		return r;
	}
	/* *c can't be 0b11111111 as this value can never appear in utf-8 */
	int bytecount = __builtin_clz(~*c & 255);
	*next = c + bytecount;
	assert_valid_read_range(&doc, c, c+bytecount);
	r = (*c) & ((128>>bytecount) - 1);
	while (bytecount > 1) r = (r << 6) | (*++c & 63);
	return r;
}

char *
writechar(char *pos, Rune r)
{
	if (r & 127) {
		*pos = r;
		return pos+1;
	}
	int bitcount = __builtin_clz(r);
	int bytecount = (bitcount - 2) / 5;
	assert_valid_write_range(&doc, pos, pos + bytecount);
	for (int i = bytecount-1; i > 0; i--) {
		pos[i] = 128 | (63 & r);
		r >>= 6;
	}
	*pos = (~0U << (8 - bytecount)) | (r & ((1<<(7 - bytecount))-1));
	return pos + bytecount;
}

bool
iswordboundry(Rune a, Rune b)
{
	return ((isspace(a) && !isspace(b)) ||
		b == '\n' || a == '\n' || a == RUNE_EOF || b == RUNE_EOF ||
		(isalpha(a) && !isalpha(b)) ||
		(isdigit(a) && !isdigit(b)) ||
		(ispunct(a) && !ispunct(b)));
}

char *
dwalkrune(const Document *d, const char *pos, int change)
{
	/* assumes document is valid utf-8 and the cursor is on a character boundary */
	if (change > 0) for (; change > 0; change--) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) break;
		assert_valid_read(d, pos);
		if (!(*pos & 128))
			pos += 1;
		else if (!(*pos & 32))
			pos += 2;
		else if (!(*pos & 16))
			pos += 3;
		else if (!(*pos & 8))
			pos += 4;
		else if (!(*pos & 4))
			pos += 5;
		else
			pos += 6;
	}
	else if (change < 0) for (; change < 0; change++) {
		if (pos == d->curright) pos = d->curleft;
		if (pos == d->bufstart) break;
		assert_valid_read(d, pos-1);
		pos--;
		while ((*pos >> 6) == 2)
			pos--;
	}
	return (char *)pos;
}

Rune
dreadchar(const Document *d, const char *pos, const char **next, int dir)
{
	assert2(dir == SIGN(dir), dir != 0);
	if (dir > 0) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) {
			*next = NULL;
			return EOF;
		}
		assert_valid_read(d, pos);
		return readchar(pos, next);
	} else if (dir < 0) {
		const char *dmy;
		if (pos == d->bufstart) {
			*next = NULL;
			return EOF;
		}
		pos = dwalkrune(d, pos, dir);
		Rune r = readchar(pos, &dmy);
		*next = pos;
		return r;
	}
	return EOF; // should never be reached
}

ssize_t
dgrowgap(Document *d, size_t change)
{
	size_t targetsize = 4 + change + (d->curleft - d->bufstart) + (d->bufend - d->curright);
	size_t oldsize = d->bufend - d->bufstart;
	size_t newsize = oldsize;
	ssize_t startshift = grow(&d->bufstart, &newsize, targetsize);
	ssize_t endshift = (newsize - oldsize) + startshift;
	if (startshift || endshift) {
		if (d->selanchor) d->selanchor += d->selanchor < d->curleft ? startshift : endshift;
		d->renderstart += d->renderstart < d->curleft ? startshift : endshift;
		d->bufend += endshift;
		d->curleft += startshift;
		d->curright += endshift;
	}
	return startshift;
}

char *
dinsert(Document *d, char *pos, char *insertstart, size_t len)
{
	pos += dgrowgap(d, len);
	char **toupdate[] = { &d->renderstart, &d->selanchor, NULL };
	if (pos <= d->curleft) {
		memmove(pos + len, pos, d->curleft - pos);
		for (char ***p = toupdate; *p; p++) {
			if (pos <= **p && **p <= d->curleft) **p += len;
		}
		d->curleft += len;
		memcpy(pos, insertstart, len);
		d->coldirty = true;
		return pos + len;
	} else {
		memmove(d->curright - len, d->curright, pos - d->curright);
		for (char ***p = toupdate; *p; p++) {
			if (d->curright <= **p && **p < pos) **p -= len;
		}
		d->curright -= len;
		memcpy(pos - len, insertstart, len);
		d->coldirty = true;
		return pos;
	}
}

char *
dinsertchar(Document *d, char *pos, Rune r)
{
	char buf[MAX_UTF8_BYTES];
	char *end = writechar(buf, r);
	return dinsert(d, pos, buf, end - buf);
}

bool
disparagraphboundry(const Document *d, const char *pos)
{
	for (;;) {
		Rune r = dreadchar(d, pos, &pos, +1);
		if (r == '\n') return true;
		else if (r == RUNE_EOF) return true;
		if (!isspace(r)) return false;
	}
	return true;
}

int
dgetcol(const Document *d, const char *pos)
{
	assert_valid_pos(d, pos);
	if (pos == d->curleft) pos = d->curright;
	const char *q;
	int col = 0;
	if (pos > d->curright) {
		assert_valid_read_range(d, d->curright, pos);
		q = memrchr(d->curright, '\n', pos - d->curright);
		if (q) {
			q++; /* start of next line */
			goto walk;
		}
	}
	assert_valid_read_range(d, d->bufstart, d->curleft);
	q = memrchr(d->bufstart, '\n', d->curleft - d->bufstart);
	if (!q) q = d->bufstart;
	else q++;
walk:
	for (;;) {
		if (q == d->curleft) q = d->curright;
		if (q == pos) return col;
		Rune r = readchar(q, &q);
		if (r == '\t') col = (col+7) & 7;
		else if (isprint(r)) col++;
	}
}

char *
dgetposnearcol(const Document *d, const char *linestart, int col)
{
	int c = 0;
	const char *pos = linestart, *q;
	while (c < col) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) break;
		Rune r = readchar(pos, &q);
		if (r == '\n') break;
		else if (r == '\t') c = (c+7) & 7;
		else if (isprint(r)) c++;
		pos = q;
	}
	return (char *)pos;
}

char *
dwalkword(const Document *d, const char *pos, int change)
{
	const char *q;	
	Rune a = dreadchar(d, pos, &q, SIGN(change)), b;
	if (a == RUNE_EOF) return (char *)pos;
	do {
		pos = q;
		b = dreadchar(d, pos, &q, SIGN(change));
	} while (!iswordboundry(a, b));
	return (char *)pos;
}

char *
dwalkrow(const Document *d, const char *pos, int change)
{
	pos = change > 0 ?
		(pos == d->curleft ? d->curright : pos) :
		(pos == d->curright ? d->curleft : pos);
	const char *end = change > 0 ?
		(pos < d->curleft ? d->curleft : d->bufend) :
		(pos > d->curright ? d->curright : d->bufstart);
	if (change > 0) {
		while (change > 0) {
			char *q = memchr(pos, '\n', end - pos);
			if (q) {
				pos = q + 1;
				change--;
			} else if (end < d->bufend) {
				pos = d->curright;
				end = d->bufend;
			} else {
				pos = end;
				break;
			}
		}
	} else if (change <= 0) {
		change--; /* walk n+1 '\n's to go back n lines */
		while (change < 0) {
			char *q = memrchr(end, '\n', pos - end);
			if (q) {
				pos = q;
				change++;
			} else if (end > d->bufstart) {
				pos = d->curleft;
				end = d->bufstart;
			} else {
				pos = NULL;
				break;
			}
		}
		if (pos) pos = dwalkrune(d, pos, +1); /* return the char after the '\n' */
		else pos = end;
	}
	return (char *)pos;
}


void
dnavigate(Document *d, char *pos, bool isselect)
{
	if (pos < d->curleft) {
		size_t change = d->curleft - pos;
		d->scrolldirty |= !!memchr(pos, '\n', change);
		memmove(d->curright - change, pos, change);
		d->curright -= change;
		d->curleft -= change;
		if (isselect && d->curleft <= d->selanchor && d->selanchor < d->curright) {
			d->selanchor += d->curright - d->curleft;
		} else if (isselect && d->selanchor == NULL) {
			d->selanchor = d->curright + change;
		}
	} else if (pos > d->curright) {
		size_t change = pos - d->curright;
		d->scrolldirty |= !!memchr(d->curright, '\n', change);
		memmove(d->curleft, d->curright, change);
		d->curright += change;
		d->curleft += change;
		if (isselect && d->curleft <= d->selanchor && d->selanchor < d->curright) {
			d->selanchor -= d->curright - d->curleft;
		} else if (isselect && d->selanchor == NULL) {
			d->selanchor = d->curleft - change;
		}
	}
	if (!isselect) d->selanchor = NULL;
	d->coldirty = true; /* it's the caller's responsibility to correct this if moving vertically */
}

void
ddeleterange(Document *d, char *left, char *right)
{
	char **toupdate[] = { &d->renderstart, &d->selanchor, NULL };
	if (left <= d->curleft && d->curright <= right) {
		if (left >= d->renderstart || right <= d->renderstart) d->scrolldirty = true;
		for (char ***p = toupdate; *p; p++) {
			if (left <= **p && **p < right) **p = left;
		}
		d->curleft = left;
		d->curright = right;
	} else if (right <= d->curleft) {
		memmove(left, right, d->curleft - right);
		for (char ***p = toupdate; *p; p++) {
			if (left <= **p && **p < right) **p = left;
			if (right < **p && **p <= d->curleft) **p -= right - left;
		}
		d->curleft -= right - left;
	} else {
		memmove(right - (left - d->curright), d->curright, left - d->curright);
		for (char ***p = toupdate; *p; p++) {
			if (left <= **p && **p <= right) **p = right;
			if (d->curright <= **p && **p <= d->curleft) **p += right - left;
		}
		d->curright += right - left;
	}
	d->coldirty = true;
}

void
ddeletesel(Document *d)
{
	assert_valid_pos(d, d->selanchor);
	if (d->selanchor <= d->curleft)
		ddeleterange(d, d->selanchor, d->curleft);
	else
		ddeleterange(d, d->curright, d->selanchor);
	d->selanchor = NULL;
}

void
dwrite(Document *d, Rune r)
{
	if (d->selanchor) ddeletesel(d);
	dgrowgap(d, MAX_UTF8_BYTES);
	d->curleft = writechar(d->curleft, r);
	d->selanchor = NULL;
	assert(d->curleft < d->curright);
}

void
dscroll(Document *d, int rowc)
{
	d->renderstart = dwalkrow(d, d->renderstart, 0);
	char *renderend = dwalkrow(d, d->renderstart, rowc);
	if (d->curleft < d->renderstart) {
		d->renderstart = dwalkrow(d, d->curleft, -rowc/2);
	} else if (d->curleft >= renderend) {
		d->renderstart = dwalkrow(d, d->curleft, -rowc/2);
	}
}

void
einit()
{
	doc.bufstart = malloc(10);
	doc.bufend = doc.bufstart + 10;
	doc.curleft = doc.bufstart;
	doc.curright = doc.bufend;
	doc.renderstart = doc.bufstart;
	doc.selanchor = NULL;
	doc.scrolldirty = true;
	doc.coldirty = true;
}

void
ewrite(Rune r)
{
	dwrite(&doc, r);
}

void
edraw(Line *line, int colc, int rowc, int *curcol, int *currow)
{
	dscroll(&doc, rowc);

	const char *p = doc.renderstart;
	Glyph g;
	for (int r = 0; r < rowc; r++) {
		bool endofline = false;
		bool tab = false;
		for (int c = 0; c < colc; c++) {
			if (p == doc.curleft) {
				*currow = endofline ? r+1 : r;
				*curcol = endofline ? 0 : c;
				p = doc.curright;
			}
			if (p == doc.bufend || endofline || tab) g.u = ' ';
			else g.u = readchar(p, &p);
			if (p == doc.curright && tab && (c & 7) == 0) {
				*currow = r;
				*curcol = c;
				tab = false;
			}
			if (g.u == '\n') {
				endofline = true;
				g.u = ' ';
			} else if (g.u == '\t') {
				tab = true;
				g.u = ' ';
			}
			g.mode = 0;
			g.fg = 1;
			g.bg = 0;
			line[r][c] = g;
		}
	}
}

void
changeindent(const Arg *arg)
{
	char *selleft = doc.selanchor ? MIN(doc.selanchor, doc.curleft) : doc.curleft;
	char *selright = doc.selanchor ? MAX(doc.selanchor, doc.curleft) : doc.curleft;
	const char *dmy;

	for (char* p = dwalkrow(&doc, selleft, 0); p < doc.bufend && p <= selright; p = dwalkrow(&doc, p, +1)) {
		if (arg->i > 0) p = dinsertchar(&doc, p, '\t');
		else if (dreadchar(&doc, p, &dmy, +1) == '\t') {
			char *q = p < doc.curright ? p : p - 1;
			ddeleterange(&doc, p, p + 1);
			p = q;
		}
	}
}

void
deletechar(const Arg *arg)
{
	if (doc.selanchor) {
		ddeletesel(&doc);
	} else if (arg->i > 0) {
		ddeleterange(&doc, doc.curright, dwalkrune(&doc, doc.curright, arg->i));
	} else if (arg->i < 0) {
		ddeleterange(&doc, dwalkrune(&doc, doc.curleft, arg->i), doc.curleft);
	}
}

void
deleteword(const Arg *arg)
{
	if (doc.selanchor) {
		ddeletesel(&doc);
	} else if (arg->i > 0) {
		ddeleterange(&doc, doc.curright, dwalkword(&doc, doc.curright, arg->i));
	} else if (arg->i < 0) {
		ddeleterange(&doc, dwalkword(&doc, doc.curleft, arg->i), doc.curleft);
	}
}

void
deleterow(const Arg *arg)
{
	(void)arg;
	if (doc.selanchor) {
		ddeletesel(&doc);
	} else {
		ddeleterange(&doc, dwalkrow(&doc, doc.curleft, 0), dwalkrow(&doc, doc.curleft, +1));
	}
}

void
navchar(const Arg *arg)
{
	char *pos = dwalkrune(&doc, doc.curleft, SIGN(arg->i));
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
navdocument(const Arg *arg)
{
	char *pos = arg->i > 0 ? doc.bufend : doc.bufstart;
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
navline(const Arg *arg)
{
	char *pos = arg->i > 0 ?
		memchr(doc.curright, '\n', doc.bufend - doc.curright) :
		memrchr(doc.bufstart, '\n', doc.curleft - doc.bufstart);
	if (pos == NULL) pos = arg->i > 0 ? doc.bufend : doc.bufstart;
	else if (arg->i < 0) pos++;
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
navpage(const Arg *arg)
{
	if (doc.coldirty) doc.col = dgetcol(&doc, doc.curleft);
	char *pos = dwalkrow(&doc, doc.curleft, SIGN(arg->i)*20);
	dnavigate(&doc, dgetposnearcol(&doc, pos, doc.col), ISSELECT(arg->i));
	doc.coldirty = false;
}

void
navparagraph(const Arg *arg)
{
	char *pos = doc.curleft;
	do {
		pos = dwalkrow(&doc, pos, SIGN(arg->i));
		if (pos == doc.bufstart && arg->i < 0) break;
		if (pos == doc.bufend && arg->i > 0) break;
	} while (!disparagraphboundry(&doc, pos));
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
navrow(const Arg *arg)
{
	if (doc.coldirty) doc.col = dgetcol(&doc, doc.curleft);
	char *pos = dwalkrow(&doc, doc.curleft, SIGN(arg->i));
	dnavigate(&doc, dgetposnearcol(&doc, pos, doc.col), ISSELECT(arg->i));
	doc.coldirty = false;
}
void
navword(const Arg *arg)
{
	char *pos = dwalkword(&doc, doc.curleft, SIGN(arg->i));
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
newline(const Arg *arg)
{
	(void)arg;
	dwrite(&doc, '\n');
}

