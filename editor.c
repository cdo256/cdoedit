/* See LICENSE for license details. */
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>

#include "editor.h"

#define MAX_LINE_LEN 1024
#define MAX_UTF8_BYTES 6

#define SIGN(a) ((a)>0 ? +1 : (a)<0 ? +1 : 0)
#define ISSELECT(a) ((a) == -2 || (a) == 2)
#define POSCMP(a,b) ((a).line == (b).line ? (b).pos - (a).pos : (b).line - (a).line);

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
char *scratchbuf;
size_t scratchlen;

/* I previously had an overly complex unrolled version of this. */
/* For simplicity and since modern compilers can optimize vector operations better than I can,
   it's probably best if I leave it to them. */
size_t
memctchr(const char *s, int c, size_t n)
{
	size_t count = 0;
	const char *end = s + n;
	for (; s < end; n--, s++)
		if (*s == c) count++;
	return count;
}

ssize_t
grow(char **buf, size_t *len, size_t newlen)
{
	if (newlen > *len) {
		*len = MAX(*len*2, newlen);
		char *newbuf;
		newbuf = realloc(*buf, *len);
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
}

Rune
readchar(const char *c, /*out*/ const char **next)
{
	Rune r;
	if (*c & 127) {
		r = *c;
		*next = c+1;
		return r;
	}
	/* *c can't be 0b11111111 as this value can never appear in utf-8 */
	int bytecount = __builtin_clz(~*c & 255);
	*next = c + bytecount;
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
		b == '\n' || a == '\n' ||
		(isalpha(a) && !isalpha(b)) ||
		(isdigit(a) && !isdigit(b)) ||
		(ispunct(a) && !ispunct(b)));
}

bool
disparagraphboundry(const Document *d, const char *pos)
{
	for (;;) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) return true;
		Rune r = readchar(pos, &pos);
		if (r == '\n') return true;
		if (!isspace(r)) return false;
	}
	return true;
}

int
dgetcol(const Document *d, const char *pos)
{
	const char *q;
	int col = 0;
	if (pos > d->curright) {
		q = memrchr(d->curright, '\n', pos - d->curright);
		if (q) {
			q++; /* start of next line */
			goto walk;
		}
	}
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
	const char *pos = linestart;
	while (c < col) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) break;
		Rune r = readchar(pos, &pos);
		if (r == '\n') break;
		else if (r == '\t') c = (c+7) & 7;
		else if (isprint(r)) c++;
	}
	return (char *)pos;
}

char *
dwalkrune(const Document *d, const char *pos, int change)
{
	/* assumes document is valid utf-8 and the cursor is on a character boundary */
	if (change > 0) for (; change > 0; change--) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) break;
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
		pos--;
		while ((*pos >> 6) == 2)
			pos--;
	}
	return (char *)pos;
}

char *
dwalkword(const Document *d, const char *pos, int change)
{
	const char *dmy;
	int dir = SIGN(change); change *= dir;
	Rune b = readchar(pos, &dmy);
	while (change > 0) {
		Rune a = b;
		pos = dwalkrune(d, pos, dir);
		b = readchar(pos, &dmy);
		if (iswordboundry(a, b)) change--;
	}
	return (char *)pos;
}

char *
dwalkrow(const Document *d, const char *pos, int change)
{
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
				pos = NULL;
				break;
			}
		}
		if (pos) pos = dwalkrune(d, pos, +1); /* return the char after the '\n' */
		else pos = end;
	} else if (change <= 0) {
		change++; /* walk n+1 '\n's to go back n lines */
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

ssize_t
dgrowgap(Document *d, size_t change)
{
	size_t targetsize = 4 + change + (d->curleft - d->bufstart) + (d->bufend - d->curright);
	size_t oldsize = d->bufend - d->bufstart;
	size_t newsize = oldsize;
	ssize_t startshift = grow(&d->bufstart, &newsize, targetsize);
	ssize_t endshift = (newsize - oldsize) + startshift;
	if (startshift || endshift) {
		d->selanchor += d->selanchor < d->curleft ? startshift : endshift;
		d->renderstart += d->renderstart < d->curleft ? startshift : endshift;
		d->bufend += endshift;
		d->curleft += startshift;
		d->curright += endshift;
	}
	return startshift;
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
		}
	} else if (pos > d->curright) {
		size_t change = pos - d->curright;
		d->scrolldirty |= !!memchr(d->curright, '\n', change);
		memmove(d->curleft, d->curright, change);
		d->curright += change;
		d->curleft += change;
		if (isselect && d->curleft <= d->selanchor && d->selanchor < d->curright) {
			d->selanchor -= d->curright - d->curleft;
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
	if (d->selanchor < d->curleft)
		ddeleterange(d, d->selanchor, d->curleft);
	else
		ddeleterange(d, d->curright, d->selanchor);
}

void
dwrite(Document *d, Rune r)
{
	dgrowgap(d, MAX_UTF8_BYTES);
	d->curleft = writechar(d->curleft, r);
}

void
einit()
{
	doc.bufstart = malloc(20000);
	doc.bufend = doc.bufstart + 20000;
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
edraw(Line *line, int rowc, int colc)
{
	const char *p = doc.renderstart;
	Glyph g;
	for (int r = 0; r < rowc; r++) {
		for (int c = 0; c < colc; c++) {
			if (p == doc.curleft) p = doc.curright;
			if (p == doc.bufend) g.u = ' ';
			else g.u = readchar(p, &p);
			if (g.u == '\n') break;
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
	char *s;
	size_t linecount = 1+memctchr(selleft, '\n', selright - selleft);
	for (s = selleft; s > doc.bufstart && s[-1] != '\n'; s--);
	size_t maxchange = linecount * MAX(arg->i, 0);
	size_t maxnewlen = maxchange + (selright-s);
	grow(&scratchbuf, &scratchlen, maxnewlen);
	ssize_t move = dgrowgap(&doc, maxchange);
	s += move;
	char *p = scratchbuf;
	for (char *p = scratchbuf; s < selright; s++, p++) {
		*p = *s;
		if (*s == '\n') {
			if (arg->i > 0) {
				for (int i = 0; i < arg->i; i++)
					*p++ = '\t';
			} else {
				for (int i = 0; i < -arg->i; i++)
					if (*s == '\t') s++;
			}
		}
	}
	memcpy(scratchbuf, selleft, p - scratchbuf);
	selleft += (p - scratchbuf);
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
		pos = dwalkrow(&doc, doc.curleft, SIGN(arg->i));
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
