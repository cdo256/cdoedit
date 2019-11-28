/* See LICENSE for license details. */
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "editor.h"

#define MAX_LINE_LEN 1024

#define SIGN(a) ((a)>0 ? +1 : (a)<0 ? +1 : 0)
#define ISSELECT(a) ((a) == -2 || (a) == 2)
#define POSCMP(a,b) ((a).line == (b).line ? (b).pos - (a).pos : (b).line - (a).line);

typedef struct {
	char data[MAX_LINE_LEN];
	int bytelen;
} DocumentLine;

typedef struct {
	char* buf;		/* buffer for storing chunks of the file */
	size_t buflen;	        /* buffer byte count, not the size of the file */
	size_t filesize;	/* the size of the file in memory if it were to be saved immediately */
	size_t startchar;       /* pos in buf of the top left */
	size_t endchar;         /* pos in buf of the bottom right */
	size_t startline;
	size_t endline;
	size_t linect;          /* number of lines */
	size_t linemax;
	DocumentLine* line;     /* the actual lines */
} Document;

typedef struct {
	int line;
	int pos;
} Pos;

/* Globals */
static Document document;
static Pos cursor;
static Pos selectionstart;
static Pos selectionend;

/* I bet this is slower than the naive method haha */
/* TODO: to make this actually run fast, used simd */
size_t
memctchr(const char *s, int c, size_t n)
{
	size_t t = 0;
	for (; ~n && ((uintptr_t)s&7)!=0; n--, s++)
		if (*s == c) t++;
	const uint64_t *a = (const uint64_t *)s;
	uint64_t v = c;
	uint64_t m = v | (v<<8) | (v<<16) | (v<<24);
	uint64_t d;;
	m |= m << 32;
	while (n >= 8) {
		d = 0ULL;
		const uint64_t *e = a + MIN(n>>3, 255);
		n -= (e-a);
		while (a < e) {
			uint64_t b = ~(*a ^ m);
			b &= b >> 1;
			b &= b >> 2;
			b &= b >> 4;
			b &= 0x0101010101010101ULL;
			d += b;
			a++;
		}
		d = (d & 0x00FF00FF00FF00FFULL) + ((d & 0xFF00FF00FF00FF00ULL)>>8);
		d = (d + (d>>16)) & 0x0000FFFF0000FFFFULL;
		d = (d + (d>>32)) & 0x00000000FFFFFFFFULL;
		t += d; 
	}
	s = (const char *)a;
	for (; ~n; n--, s++)
		if (*s == c) t++;
	return t;
}

int
userwarning(const char *s, ...)
{
	va_list va;
	va_start(va, s);
	vfprintf(stderr, s, va);
	va_end(va); 
}

#if 0
const char *
skiplines(const char *bufstart, const char *bufend, const char *bufpos, int change)
{
	int dir = SIGN(change);
	int count = dir * change;
	const char *p = bufpos;
	for (int i = 0; i < count; i++) {
		if (dir > 0) {
			p = memchr(p, '\n', bufend - p);
		} else {
			p = memrchr(bufstart, '\n', p - bufstart);
		}
		if (!p && i == count-1) {
			return dir>0 ? bufend : bufstart;
		} else if (!p) {
			return NULL;
		}
	}
	return p;
}

const char *
dgetouter(const Document *d, Pos targetpos, Pos startpos, const char* startptr)
{
	if (startptr
}

int
dlinelength(const Document *d, int line)
{
	if (line < d->startline) {
		int linepos = skipline(d);
		char *p = memchr(d->buf + linepos, '\n', d->startchar);
		if (p) return p - (d->buf + linepos);
		else return d->startchar - linepos;
	} else if (line >= d->endline) {
		int linepos = dcountforwardline(d, line 
}

Rune
dgetchar(const Document *d, Pos pos)
{
	if (pos.line < d->startline) {
		int linepos = dcountbackline(d, pos.line);
		if (~linepos) return d->buf[linepos + pos.pos];
	} else if (pos.line > d->endline) {
		int linepos = dcountforwardline(d, pos.line);
		if (~linepos) return d->buf[linepos + pos.pos];
	} else {
		DocumentLine *line = &d->line[pos.line - d->startline];
		if (pos.pos < line->bytelen) {
			return d->line[pos.line - d->startline].data[pos.pos];
		} else if (pos.pos == line->bytelen) return '\n';
	}
	return '\0';
}

int
iswhitespace(Rune c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int
iswordboundry(Rune a, Rune b)
{
	return (iswhitespace(a) && !iswhitespace(b)) || b == '\n' || a == '\n' ||
		(isalpha(a) && !isalpha(b)) ||
		(isnum(a) && !isalpha(b)) ||
		(issymbol(a) && !issymbol(b));
}

Pos
djumpword(const Document *d, Pos pos, int change)
{
	int dir = MAX(MIN(-1, change), +1);
	if (change == 0) return pos;
	Rune prev = dgetchar(d, pos);
	for (int i = 0; i != change; i += dir) {
		for (;; pos.pos++) {
			if (pos.pos > d->line[pos.line - d->startline].bytelen) {
				if (line >= d->endline) return pos;
				pos.line++;
				pos.pos = 0;
			}
			if (i != 0 && iswordboundry(prev, dgetchar(d, pos))) {
				break;
			} 
		}
	}
	return pos;
}

void
dgrowdown(Document *d, int start)
{
	
}

void
dscrolltopos(Document* d)
{
	
}

void
ddeletelines(Document *d, int start, int end)
{
	if (start < 0) {
		int startchar = dcountbackline(d, start);
		if (!~startchar) startchar = 0;
		int diff = d->startchar - startchar;
		d->buflen -= diff;
		d->startchar = startchar;
		start = 0;
	}
	if (end >= d->linect) {
		int endchar = dcountforwardline(d, end);
		if (!~endchar) endchar = d->buflen;
		int diff = endchar - d->endchar;
		d->buflen -= diff;
		d->endchar = endchar;
		end = d->linect - 1;
	}
	int change = 0;
	for (int i = start; i <= end; i++) {
		change += 1 + d->line[i].bytelen;
	}
	memmove(d->line + start, d->line + end+1, sizeof(DocumentLine)*(end+1-start));
	d->linect -= end+1 - start;
	d->filesize -= change;
	dgrowdown(d, end+1 - start);
}

void
ddeleterange(Document *d, int line1, int pos1, int line2, int pos2)
{
	if (line2 - 1 >= line1 + 1) {
		ddeletelines(d, line1+1, line2-1);
		line2 = line1 + 1;
	}
	if (line1 < 0) {
		int startchar = dcountbackline(d, line1);
		if (~startchar) startchar += pos1;
		int diff = d->startchar - startchar;
		d->buflen -= diff;
		d->startchar = startchar;
		line1 = 0;
		pos1 = 0;
	}
	if (line2 >= d->linect) {
		int endchar = dcountforwardline(d, line2);
		if (~endchar) endchar += pos2;
		int diff = endchar - d->endchar;
		d->buflen -= diff;
		d->endchar = endchar;
		line2 = d->linect;
		pos2 = 0;
	}
	if (line1 == line2 && line1 < d->linect) {
		memmove(d->line[line1].data + pos1, d->line[line1].data + pos2, d->line[line1].bytelen - pos2);
		d->line[line1].bytelen -= pos2 - pos1;
	} else {
		int linelen = pos1 + d->line[line2].bytelen - pos2;
		if (linelen > MAX_LINE_LEN) {
			userwarning("this deletion would result in a line that's too long");
			return;
		}
		memcpy(d->line[line1].data, d->line[line2].data, pos2);
		d->line[line1].bytelen = linelen;
		ddeletelines(d, line1+1, line2);
	}
}

void
changeindent(const Arg *arg)
{
	DocumentLine* line = &document.line[cursor.line];
	if (arg->i > 0) { 
		if (line->bytelen + arg->i >= MAX_LINE_LEN) {
			userwarning("no more space left in line");
			return;
		}
		memmove(line->data, line->data + arg->i, line->bytelen);
		for (int i = 0; i < arg->i; i++) {
			line->data[i] = '\t';
		}
		line->bytelen += arg->i;
		document.filesize += arg->i;
	} else if (arg < 0) {
		for (int i = 0; i < -arg->i; i++) {
			if (line->data[i] != '\t') {
				userwarning("line doesn't begin with %d tabs", -arg->i);
				return;
			}
		}
		ddeleterange(&document, cursor.line, 0, cursor.line, -arg->i);
	}
}

void
deletechar(const Arg *arg)
{
	int min = MIN(cursor.pos, cursor.pos + arg->i);
	int max = MAX(cursor.pos, cursor.pos + arg->i);
	int minline = cursor.line;
	while (min < 0) {
		min += document.line[minline].bytelen + 1;
		minline--;
	}
	int maxline = cursor.line;
	while (max > document.line[maxline].bytelen) {
		max -= document.line[maxline].bytelen + 1;
		maxline++;
	}
	ddeleterange(&document, minline, min, maxline, min);
}

void
deleteword(const Arg *arg)
{
	Pos pos = djumpword(&document, cursor, arg->i);
	pos = dnormalisepos(&docunent, pos);
	edeleterange(&document, cursor, pos);
}

void
dnavigate(Document *d, Pos pos, int control)
{
	pos = dnormalisepos(&document, pos);
	if (arg->i == 2) {
		selectionend = pos;
		if (selectionstart.line == -1) selectionstart = cursor;
	} else if (arg->i == -2) {
		selectionstart = pos;
		if (selectionend.line == -1) selectionend = cursor;
	} else selectionstart = selectionend = {-1,-1};
	cursor = pos;
	dupdateinner();
}
#endif

DocumentLine *
dgetinnerline(const Document *d, int line)
{
	if (d->startline <= line && line <= d->endline) {
		return &d->line[line - d->startline];
	} else return NULL;
}


Pos
dnormalisepos(const Document *d, Pos pos)
{
	DocumentLine *line = dgetinnerline(d, line);
	/* most common case first */
	if (line && pos >= 0 && pos <= line.bytelen) return pos;
	if (pos.pos < 0) {
		if (pos.line > d->endline) {
			const char *bufpos = d->buf + d->endchar;
			const char *linepos = skiplines(d->buf + d->endchar, d->buf + d->buflen, bufpos, pos.line - d->endline);
			if (linepos + ct < d->buf + d->endchar) 

			size_t ct = memctchr(linepos, '\n', d->buf + d->buflen - linepos);
		while (pos.pos < 0) {
			if (pos.line <= d->startline) {
				if (~linepos) pos.line = linepos;
				else {
					pos.line = 1;
					pos.pos = 0;
				}
			} else if (pos.line > d->endline) {
				int linepos = dcountbackline(d, pos.line);
				if (~linepos) pos.line = linepos;
				else {
					pos.line = d->lastline;
					char *p = memrchr(d->buf + d->endchar, '\n', d->buflen - d->endchar);
					if (p) pos.pos = d->buflen - d->endchar;
					else pos.pos = d->buf + d->buflen - p + 1;
					return pos;
				}
			}
		}
	} else {
		int linelen;
		do {
			if (line = dgetinnerline(d, pos)) {
				if (line->	
			} else if (pos.line < d->startline) {
			} else if (pos.line >= d->endline) {
		} while (pos.pos > linelen);
		
	}
}

Pos
dadvancerune(const Document *d, Pos pos, int dir)
{
	DocumentLine *line = dgetinnerline(d, pos.line);
	if (pos.pos == line->bytelen && dir > 0) {
		pos.line++;
		pos.pos = 0;
		return pos;
	} else if (pos.pos == line->bytelen && dir < 0) {
		pos.pos--;
		return dnormalisepos(d, pos);
	} else if (pos.pos == 0 && dir < 0) {
		pos.pos--;
		return dnormalisepos(d, pos);
	}
	unsigned c = line->data[pos];
	if (!(c & 0x80)) return pos + dir;
	if (dir > 0) {
		if (!(c & 0x40)) {
			while (pos < line->bytelen && (line->data[pos] & 0x80) && !(line->data[pos] & 0x40))
				pos++;
		} else if (!(c & 0x20))
			pos += 2;
		else if (!(c & 0x10))
			pos += 3;
		else if (!(c & 0x08))
			pos += 4;
		else if (!(c & 0x04))
			pos += 5;
		else if (!(c & 0x02))
			pos += 6;
		pos.pos = MAX(pos.pos, line->bytelen);
	} else {
		while (!(line->data[pos] & 0x40) && pos > 0)
			pos++;
	}
	return pos;
}

void
navchar(const Arg *arg)
{
	Pos pos = cursor;
	pos.pos = ladvancerune(&document, pos.pos, SIGN(arg->i));
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}
void
navdocument(const Arg *arg)
{
	Pos pos = {1, 0};
	if (arg->i > 0) {
		pos.line = document.totallinecount;
		pos.pos = dgetlinelen(&document, pos.line);
	}
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}
void
navline(const Arg *arg)
{
	Pos pos = cursor;
	pos.pos = arg < 0 ? 0 : dgetlinelen(&document, pos.line);
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}
void
navpage(const Arg *arg)
{
	Pos pos = cursor;
	DocumentLine *line = dgetlineinner(d, pos.line);
	int col = dbytetocolumn(&document, line, pos.pos);
	pos.line += 20 * SIGN(arg->v);
	pos.pos = dcolumntobyte(&document, pos.line, col);
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}

void
navparagraph(const Arg *arg)
{
	Pos pos = cursor;
	DocumentLine *line = dgetlineinner(d, pos.line);
	int col = dbytetocolumn(&document, line, pos.pos);
	while (dgetlinelen(&document, pos.line) > 0 && BETWEEN(pos.line, 1, document.totallinecount)) 
		pos.line += SIGN(arg->v);
	pos.pos = dcolumntobyte(&document, pos.line, col);
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}
void
navrow(const Arg *arg)
{
	Pos pos = cursor;
	DocumentLine *line = dgetlineinner(d, pos.line);
	int col = dbytetocolumn(&document, line, pos.pos);
	pos.line += SIGN(arg->v);
	pos.pos = dcolumntobyte(&document, pos.line, col);
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}
void
navword(const Arg *arg)
{
	Pos pos = cursor;
	pos = djumpword(&document, pos, SIGN(arg->i))
	dnavigate(&document, pos, arg->i);
	scrolltocursor();
}
void
newline(const Arg *arg)
{
	dsplitline(&document, cursor);
}
