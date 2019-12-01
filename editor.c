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
readchar(const char *c)
{
	Rune r;
	if (*c & 127) return *c;
	/* *c can't be 0b11111111 as this value can never appear in utf-8 */
	int bytecount = __builtin_clz(~*c & 255);
	r = (*c) & ((128>>bytecount) - 1);
	while (bytecount > 1) r = (r << 6) | (*++c & 63);
	return r;
}

bool
iswhitespace(Rune c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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
disparagraphboundry(const Document *d, const char *pos, int dir)
{
	for (;; pos++) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend || *pos == '\n') return true;
		if (!isspace(readchar(pos))) return false;
	}
	return false;
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
changeindent(const Arg *arg)
{
	char *selleft = doc.selanchor ? MIN(doc.selanchor, doc.curleft) : doc.curleft;
	char *selright = doc.selanchor ? MAX(doc.selanchor, doc.curleft) : doc.curleft;
	char *s;
	size_t linecount = 1+memctchr(selleft, '\n', selright - selleft);
	for (s = selleft; s > doc.bufstart && s[-1] != '\n'; s--);
	size_t maxchange = MAX(linecount * arg->i, 0);
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

void ddeleterange(Document *d, char *left, char *right)
{
	if (left <= d->curleft && d->curright <= right) {
		d->curleft = left;
		d->curright = right;
		if (left >= d->renderstart || right <= d->renderstart) d->scrolldirty = true;
		dnavigate(d, d->curleft, false);
	} else
}

void ddeletesel(Document *d)
{
	if (d->selanchor < d->curleft)
		ddeleterange(d, d->selanchor, d->curleft);
	else
		ddeleterange(d, d->cur, d->selanchor);
}

void
deletechar(const Arg *arg)
{
	if (doc.selanchor) {
		ddeletesel(&doc);
	} else if (arg->i > 0) {
		ddeleterange(&doc, doc.curright, dwalkrune(doc.curright, arg->i));
	} else if (arg->i < 0) {
		ddeleterange(&doc, dadvancerune(doc.curleft, arg->i), doc.curleft);
	}
}

void
deleteword(const Arg *arg)
{
	if (selleft && selright) {
		ddeletesel(&doc);
	} else if (arg->i > 0) {
		ddeleterange(&doc, doc.curright, dwalkword(&doc, doc.curright, arg->i));
	} else if (arg->i < 0) {
		ddeleterange(&doc, dwalkword(&doc, doc.curleft, arg->i), doc.curleft);
	}
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

char *
dwalkrune(const Document *d, char *pos, int change)
{
	/* assumes document is valid utf-8 and the cursor is on a character boundary */
	if (charge > 0) for (; change > 0; change--) {
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
	else if (charge < 0) for (; change < 0; change++) {
		if (pos == d->curright) pos = d->curleft;
		if (pos == d->bufstart) break;
		pos--;
		while ((*pos >> 6) == 2)
			pos--;
	}
	return pos;
}

char *
dwalkrow(const Document *d, char *pos, int change, bool keepcol)
{
	if (keepcol && d->coldirty) {
		d->col = dgetcol(d, pos);
		d->coldirty = false;
	}
	char *end = change > 0 ? d->curleft : d->curright;
	if (change > 0) {
		char *end = pos < d->curleft ? d->curleft : d->bufend;
		while (change > 0) {
			char *q = memchr(pos, '\n', end - pos);
			if (q) {
				pos = q + 1;
				change--;
			} else if (end < d->bufend) {
				pos = d->curright;
			} else {
				pos = NULL;
				break;
			}
		}
		if (pos) pos = dwalkrune(d, pos, +1); /* return the char after the '\n' */
		else pos = end;
	} else if (change < 0) {
		change++; /* walk n+1 '\n's to go back n lines */
		char *end = pos > d->curright ? d->curright : d->bufstart;
		while (change < 0) {
			char *q = memrchr(end, '\n', pos - end);
			if (q) {
				change++;
				pos = q;
			} else if (end > d->bufstart) {
				pos = d->curleft;
			} else {
				pos = NULL;
				break;
			}
		}
		if (pos) pos = dwalkrune(d, pos, +1); /* return the char after the '\n' */
		else pos = end;
	}
	return keepcol ? dgetposnearcol(d, pos, col) : pos;
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
	char *pos = arg->i > 0 ? memchr(doc.curright, '\n', doc.bufend - doc.curright) : memrchr(doc.bufstart, '\n', doc.curleft - doc.bufstart);
	if (pos == NULL) pos = arg->i > 0 ? doc.bufend : doc.bufstart;
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
navpage(const Arg *arg)
{
	char *pos = dwalkrow(&doc, doc.curleft, SIGN(arg->i)*20, true);
	dnavigate(&doc, pos, ISSELECT(arg->i));
	doc.coldirty = false;
}

void
navparagraph(const Arg *arg)
{
	char *pos = doc.curleft;
	do {
		pos = dwalkrow(&doc, doc.curleft, SIGN(arg->i), false);
	} while (!disparagraphbreak(&doc, q, SIGN(arg->i));
	dnavigate(&doc, pos, ISSELECT(arg->i));
}
void
navrow(const Arg *arg)
{
	char *pos = dwalkrow(&doc, doc.curleft, SIGN(arg->i)), true);
	dnavigate(&doc, pos, ISSELECT(arg->i));
	doc.coldirty = false;
}
void
navword(const Arg *arg)
{
	char *pos = dwalkword(&document, doc.curleft, SIGN(arg->i));
	dnavigate(&document, pos, ISSELCT(arg->i));
}
void
newline(const Arg *arg)
{
	dwrite(&document, '\n');
}
