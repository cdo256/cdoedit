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

typedef enum {
	KEEPONDELETE = 1,
	NULLONDELETE = 2,
	PREFERLEFT = 4,
	PREFERRIGHT = 8,
	HOLDONNAVIGATE = 16,
	SLIDEONNAVIGATE = 32,
} UpdateSetEntryBehaviour;

#define UpdateSetEntryBehaviourDefault KEEPONDELETE | PREFERLEFT | HOLDONNAVIGATE

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
#define assert_valid_behaviour(b) assert3(!(b & KEEPONDELETE) || !(b & NULLONDELETE), !(b & PREFERLEFT) || !(b & PREFERRIGHT), !(b & HOLDONNAVIGATE) || !(b & SLIDEONNAVIGATE));

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
	char **ptr;
	char *newval; /* temporary value for updating */
	int count; /* no times appears in multiset */
	UpdateSetEntryBehaviour behaviour;
} UpdateSetEntry;

/* multiset so add and delete are symmetric */
typedef struct {
	size_t cap;
	size_t count;
	UpdateSetEntry *array;
} UpdateSet;

typedef struct {
	char *bufstart;		/* buffer for storing chunks of the file */
	char *bufend;	        /* one past the end of the buffer */
	char *curleft;		/* one past the end of the upper section */
	char *curright;         /* start of the lower section */
	char *renderstart;      /* top left of the editor */
	char *selanchor;
	bool coldirty;
	int col;
	UpdateSet us;
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

bool
grow(void **buf, size_t *len, size_t newlen, size_t entrysize)
{
	assert6(buf, len, !*buf == !*len, !STUPIDLY_BIG(*len), !STUPIDLY_BIG(newlen), entrysize < 65536);
	if (newlen > *len) {
		*len = MAX(*len*2, newlen);
		char *newbuf;
		newbuf = realloc(*buf, *len * entrysize);
		if (!newbuf) {
			fail();
			fprintf(stderr, "could not realloc\n");
			exit(1);
		}
		*buf = newbuf;
		return true;
	}
	return false;
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
	assert((unsigned)*c != 255);
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

/* needed for cases where there's dependencies between pointers (eg. most shifts rely on d->curleft) */
void
usflip(UpdateSet *us)
{
	for (size_t i = 0; i < us->count; i++) {
		*us->array[i].ptr = us->array[i].newval;
		us->array[i].newval = NULL; /* help future me when I get 'round to debugging this mess */
	}
#ifdef DEBUG
	for (size_t i = 0; i < us->count; i++) {
		if (*us->array[i].ptr)
			assert_valid_pos(*us->array[i].ptr);
	}
#endif
}

/* these two functions only need to be used on paramaters and local variables in functions that use non-const Document */
void
usadd(UpdateSet *us, char **ptr, UpdateSetEntryBehaviour behaviour)
{
	assert_valid_behaviour(behaviour);
	for (size_t i = 0; i < us->count; i++) {
		if (us->array[i].ptr == ptr) {
			assert(behaviour == us->array[i].behaviour);
			us->array[i].behaviour |= behaviour;
			us->array[i].count++;
			assert_valid_behaviour(us->array[i].behaviour);
			return;
		}
	}
	grow((void**)&us->array, &us->cap, us->count+1, sizeof(us->array[0]));
	us->array[us->count].ptr = ptr;
	us->array[us->count].count = 1;
	us->array[us->count].behaviour = behaviour;
	us->count++;
}

void
usremv(UpdateSet *us, char **ptr)
{
	for (size_t i = 0; i < us->count; i++) {
		if (us->array[i].ptr == ptr) {
			us->array[i].count--;
			if (us->array[i].count == 0) {
				us->array[i] = us->array[us->count-1];
				us->count--;
				return;
			}
		}
	}
	fail();
}

void
usinit(UpdateSet *us)
{
	us->count = 0;
	us->cap = 50;
	us->array = malloc(us->cap*sizeof(UpdateSetEntry));
}

void
dupdateongrow(Document *d, char *newstart, char *newend)
{
	for (size_t i = 0; i < d->us.count; i++) {
		char *u = *d->us.array[i].ptr, *v = NULL;
		if (u != NULL) {
			assert_valid_pos(d, u);
			if (u <= d->curleft)
				v = u + (newstart - d->bufstart);
			else
				v = u + (newend - d->bufend);
			assert(v);
		} else v = u;
		d->us.array[i].newval = v;
	}
	usflip(&d->us);
}

void
dupdateondelete(Document *d, char *rangestart, char *rangeend)
{
	assert_valid_write_range(d, rangestart, rangeend);
	for (size_t i = 0; i < d->us.count; i++) {
		char *u = *d->us.array[i].ptr, *v = NULL;
		UpdateSetEntryBehaviour behaviour = d->us.array[i].behaviour;
		if (u != NULL) {
			assert_valid_pos(d, u);
			if (d->curright <= rangestart) {
				v =	u < d->curright ? u :
					u < rangestart ? u + (rangeend - rangestart) :
					u < rangeend ? ((behaviour & NULLONDELETE) ? NULL : rangeend) : u;
			} else if (d->curleft >= rangeend) {
				v = 	u < rangestart ? u :
					u < rangeend ? ((behaviour & NULLONDELETE) ? NULL : rangestart) :
					u <= d->curleft ? u - (rangeend - rangestart) : u;
			} else {
				assert3(rangestart < d->curleft, d->curleft < d->curright, d->curright < rangeend);
				if (rangestart <= u && u < rangeend) {
					v = 	(behaviour & NULLONDELETE) ? NULL :
						(behaviour & PREFERRIGHT) ? rangeend : rangestart;
				} else v = u;
			}
			assert((behaviour & NULLONDELETE) || v);
		} else v = u;
		d->us.array[i].newval = v;
	}
	usflip(&d->us);
}

void
dupdateoninsert(Document *d, char *pos, size_t len)
{
	assert_valid_pos(d, pos);
	for (size_t i = 0; i < d->us.count; i++) {
		char *u = *d->us.array[i].ptr, *v;
		UpdateSetEntryBehaviour behaviour = d->us.array[i].behaviour;
		if (u != NULL) {
			assert_valid_pos(d, u);
			if (d->curright <= pos) {
				if (d->curright <= u && u < pos) v = u - len;
				else if (u == pos) v = (behaviour & PREFERRIGHT) ? u : u - len;
				else v = u;
			} else {
				if (u == pos) v = (behaviour & PREFERRIGHT) ? u + len : u;
				else if (pos < u && u <= d->curleft) v = u + len;
				else v = u;
			}
			assert(v);
		} else v = u;
		d->us.array[i].newval = v;
	}
	usflip(&d->us);
}

void
dupdateonnavigate(Document *d, char *newcursor)
{
	assert_valid_pos(d, newcursor);
	for (size_t i = 0; i < d->us.count; i++) {
		char *u = *d->us.array[i].ptr, *v;
		UpdateSetEntryBehaviour behaviour = d->us.array[i].behaviour;
		if (u != NULL) {
			assert_valid_pos(d, u);
			if (d->curright <= newcursor) {
				if ((behaviour & SLIDEONNAVIGATE) && (u == d->curright || u == d->curleft)) {
					v = u + (newcursor - d->curright);
				} else {
					v =	u <= d->curleft ? u :
						u < newcursor ? u - (d->curright - d->curleft) : u;
				}
			} else {
				if ((behaviour & SLIDEONNAVIGATE) && (u == d->curright || u == d->curleft)) {
					v = u - (d->curleft - newcursor);
				} else {
					v =	u <= newcursor ? u :
						u <= d->curleft ? u + (d->curright - d->curleft) : u;
				}
			}
			assert(v);
		} else v = u;
		d->us.array[i].newval = v;
	}
	usflip(&d->us);
}

void
dgrowgap(Document *d, size_t change)
{
	size_t targetsize = MAX_UTF8_BYTES + change + (d->curleft - d->bufstart) + (d->bufend - d->curright);
	size_t oldsize = d->bufend - d->bufstart;
	size_t newsize = oldsize;
	char *newbuf = d->bufstart;
	if (grow((void**)&newbuf, &newsize, targetsize, 1)) {
		dupdateongrow(d, newbuf, newbuf + newsize);
	}
}

void
dinsert(Document *d, char *pos, char *insertstr, size_t len)
{
	usadd(&d->us, &pos, 0);
	dgrowgap(d, len);
	if (pos <= d->curleft) {
		memmove(pos + len, pos, d->curleft - pos);
		memcpy(pos, insertstr, len);
	} else {
		memmove(d->curright - len, d->curright, pos - d->curright);
		memcpy(pos - len, insertstr, len);
	}
	d->coldirty = true;
	dupdateoninsert(d, pos, len);
	usremv(&d->us, &pos);
}

void
dinsertchar(Document *d, char *pos, Rune r)
{
	usadd(&d->us, &pos, 0);
	char buf[MAX_UTF8_BYTES];
	char *end = writechar(buf, r);
	dinsert(d, pos, buf, end - buf);
	usremv(&d->us, &pos);
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
	if (isselect && !d->selanchor) d->selanchor = d->curright;
	else if (!isselect) d->selanchor = NULL;
	if (pos <= d->curleft) {
		memmove(d->curright - (d->curleft - pos), pos, d->curleft - pos);
	} else if (pos >= d->curright) {
		memmove(d->curleft, d->curright, pos - d->curright);
	}
	dupdateonnavigate(d, pos);
	d->coldirty = true; /* it's the caller's responsibility to correct this if moving vertically */
}

void
ddeleterange(Document *d, char *left, char *right)
{
	if (left <= d->curleft && d->curright <= right) {
	} else if (right <= d->curleft) {
		memmove(left, right, d->curleft - right);
	} else {
		memmove(right - (left - d->curright), d->curright, left - d->curright);
	}
	dupdateondelete(d, left, right);
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
	doc.coldirty = true;
	usinit(&doc.us);
	usadd(&doc.us, &doc.bufstart, PREFERLEFT);
	usadd(&doc.us, &doc.bufend, PREFERRIGHT);
	usadd(&doc.us, &doc.curleft, PREFERRIGHT | SLIDEONNAVIGATE);
	usadd(&doc.us, &doc.curright, PREFERLEFT | SLIDEONNAVIGATE);
	usadd(&doc.us, &doc.selanchor, 0);
	usadd(&doc.us, &doc.renderstart, 0);
}

void
ewrite(Rune r)
{
	dinsertchar(&doc, doc.curleft, r);
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
	usadd(&doc.us, &selleft, 0);
	usadd(&doc.us, &selright, 0);
	const char *dmy;

	char *p = dwalkrow(&doc, selleft, 0);
	usadd(&doc.us, &p, 0);
	for (; p < doc.bufend && p <= selright; p = dwalkrow(&doc, p, +1)) {
		if (arg->i > 0) dinsertchar(&doc, p, '\t');
		else if (dreadchar(&doc, p, &dmy, +1) == '\t') ddeleterange(&doc, p, p + 1);
	}
	usremv(&doc.us, &p);
	usremv(&doc.us, &selleft);
	usremv(&doc.us, &selright);
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
	dinsertchar(&doc, doc.curleft, '\n');
}

