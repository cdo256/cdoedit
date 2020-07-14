/* See LICENSE for license details. */
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.c"
#include "editor.h"

typedef enum {
	LEFTONDELETE = 1,
	RIGHTONDELETE = 2,
	NULLONDELETE = 4,
	LEFTONINSERT = 8,
	RIGHTONINSERT = 16,
	HOLDONNAVIGATE = 32,
	SLIDEONNAVIGATE = 64,
} UpdateSetEntryBehaviour;

typedef enum {
	NOP,
	INSERT,
	DELETE,
} ActionType;

#define ISSELECT(a) ((a) == -2 || (a) == 2)
#define POSCMP(d, a, b) ( \
	((a) >= (d)->curright ? (a) - ((d)->curright - (d)->curleft) : (a)) - \
	((b) >= (d)->curright ? (b) - ((d)->curright - (d)->curleft) : (b)))

#define assert_valid_pos(d, x) \
	assert3((d)->bufstart <= (x), (x) <= (d)->curleft || (d)->curright <= (x), (x) <= (d)->bufend)
#define assert_valid_read(d, x) \
	assert_valid_pos(d,x); assert2((x) != (d)->curleft, (x) != (d)->bufend)
#define assert_valid_write(d, x) assert2((d)->bufstart <= (x), (x) < (d)->bufend)
#define assert_valid_read_range(d, x, y) \
	assert4((x) <= (y), (d)->bufstart <= (x), (y) <= (d)->curleft || (d)->curright <= (x), (y) <= (d)->bufend)
#define assert_valid_write_range(d, x,y) assert2((d)->bufstart <= (x), (y) <= (d)->bufend)
#define assert_valid_behaviour(b) assert3(!(b & LEFTONDELETE) || !(b & RIGHTONDELETE) || !(b & NULLONDELETE), !(b & LEFTONINSERT) || !(b & RIGHTONINSERT), !(b & HOLDONNAVIGATE) || !(b & SLIDEONNAVIGATE));

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
	char *bufstart;         /* buffer for storing chunks of the file */
	char *bufend;           /* one past the end of the buffer */
	char *curleft;          /* one past the end of the upper section */
	char *curright;         /* start of the lower section */
	char *renderstart;      /* top left of the editor */
	char *selanchor;
	bool coldirty;
	int col;
	UpdateSet us;
} Document;

typedef struct {
	ActionType type;
	size_t position;
	size_t size;
	char *data;
} Action;

typedef struct {
	Action *a;
	size_t count;
	size_t max;
	size_t cur;
} History;

/* Globals */
static Document doc;
static History history;
char *filename = NULL;

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
		if (!((uchar)*pos & 128))
			pos += 1;
		else if (!((uchar)*pos & 32))
			pos += 2;
		else if (!((uchar)*pos & 16))
			pos += 3;
		else if (!((uchar)*pos & 8))
			pos += 4;
		else if (!((uchar)*pos & 4))
			pos += 5;
		else
			pos += 6;
	}
	else if (change < 0) for (; change < 0; change++) {
		if (pos == d->curright) pos = d->curleft;
		if (pos == d->bufstart) break;
		assert_valid_read(d, pos-1);
		pos--;
		while (((uchar)*pos >> 6) == 2)
			pos--;
	}
	return (char *)pos;
}

Rune
dreadchar(const Document *d, const char *pos, const char **next, int dir)
{
	assert2(dir == SIGN(dir), dir != 0);
	Rune r;
	if (dir > 0) {
		if (pos == d->curleft) pos = d->curright;
		if (pos == d->bufend) {
			*next = NULL;
			return RUNE_EOF;
		}
		assert_valid_read(d, pos);
		Rune r;
		*next = pos + utf8decode(pos, &r, d->bufend - pos);
		return r;
	} else if (dir < 0) {
		if (pos == d->bufstart) {
			*next = NULL;
			return RUNE_EOF;
		}
		pos = dwalkrune(d, pos, dir);
		utf8decode(pos, &r, d->bufend - pos);
		*next = pos;
		return r;
	}
	return RUNE_EOF; // should never be reached
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
	us->array = grow(us->array, &us->cap, us->count+1, sizeof(us->array[0]));
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

bool
usinit(UpdateSet *us)
{
	us->count = 0;
	us->cap = 50;
	us->array = umalloc(us->cap*sizeof(UpdateSetEntry));
	if (!us->array) {
		us->cap = 0;
		return false;
	}
	return true;
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
				assert3(rangestart <= d->curleft, d->curleft < d->curright, d->curright <= rangeend);
				if (POSCMP(d, rangestart, u) <= 0 && POSCMP(d, u, rangeend) <= 0) {
					v = 	(behaviour & NULLONDELETE) ? NULL :
						(behaviour & RIGHTONDELETE) ? rangeend : rangestart;
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
				else if (u == pos) v = (behaviour & RIGHTONINSERT) ? u : u - len;
				else v = u;
			} else {
				if (u == pos) v = (behaviour & RIGHTONINSERT) ? u + len : u;
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
	size_t targetsize = UTF_SIZ + change + (d->curleft - d->bufstart) + (d->bufend - d->curright);
	size_t oldsize = d->bufend - d->bufstart;
	size_t newsize = oldsize;
	char *newbuf = grow(d->bufstart, &newsize, targetsize, 1);
	if (newbuf != d->bufstart || newsize != oldsize) {
		memmove(newbuf + newsize - (d->bufend - d->curright), newbuf + (d->curright - d->bufstart), d->bufend - d->curright);
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
	const char *q;
	int col = 0;
	if (pos >= d->curright) {
		assert_valid_read_range(d, d->curright, pos);
		q = memrchr(d->curright, '\n', pos - d->curright);
		if (q) {
			q++; /* start of next line */
			goto walk;
		}
	}
	assert_valid_read_range(d, d->bufstart, pos);
	q = memrchr(d->bufstart, '\n', pos - d->bufstart);
	if (!q) q = d->bufstart;
	else q++;
walk:
	for (;;) {
		if (0 == POSCMP(d, q, pos)) return col;
		Rune r = dreadchar(d, q, &q, +1);
		if (r == '\t') col = ((col+8) & ~7);
		else if (isprint(r)) col++;
		else if (r == RUNE_EOF) return col;
	}
}

char *
dgetposnearcol(const Document *d, const char *linestart, int col)
{
	int c = 0;
	const char *pos = linestart, *q;
	while (c < col) {
		Rune r = dreadchar(d, pos, &q, +1);
		if (r == '\n') break;
		else if (r == '\t') c = ((c+8) & ~7);
		else if (isprint(r)) c++;
		else if (r == RUNE_EOF) return (char *)pos;
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

/* return value heap-allocated and null terminated */
char *
dgetsubstr(const Document *d, char *start, char *end)
{
	assert(start <= end);
	const size_t gapsize = d->curright - d->curleft;
	if (end <= d->curleft || d->curright <= start) {
		char *ret = umalloc(end - start + 1);
		memcpy(ret, start, end - start);
		ret[end - start] = '\0';
		return ret;
	} else {
		char *ret = umalloc(end - start - gapsize + 1);
		memcpy(ret, start, d->curleft - start);
		memcpy(ret + (d->curleft - start), d->curright, end - d->curright);
		ret[end - start - gapsize] = '\0';
		return ret;
	}
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
		/* nothing needs to be done in here */
	} else if (right <= d->curleft) {
		memmove(left, right, d->curleft - right);
	} else {
		memmove(right - (left - d->curright), d->curright, left - d->curright);
	}
	dupdateondelete(d, left, right);
	d->coldirty = true;
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

/* content must appear at end of buffer */
bool
dinit(Document *d, char *buf, size_t buflen, size_t contentlen)
{
	d->bufstart = buf;
	d->bufend = buf + buflen;
	d->curleft = buf;
	d->curright = (buf + buflen) - contentlen;
	d->renderstart = buf;
	d->selanchor = NULL;
	d->coldirty = true;
	if (!usinit(&d->us)) return false;
	usadd(&d->us, &d->bufstart, LEFTONINSERT | LEFTONDELETE);
	usadd(&d->us, &d->bufend, RIGHTONINSERT | RIGHTONDELETE);
	usadd(&d->us, &d->curleft, RIGHTONINSERT | LEFTONDELETE | SLIDEONNAVIGATE);
	usadd(&d->us, &d->curright, LEFTONINSERT | RIGHTONDELETE | SLIDEONNAVIGATE);
	usadd(&d->us, &d->selanchor, 0);
	usadd(&d->us, &d->renderstart, 0);
	return true;
}

void
dfree(/* move */ Document *d)
{
	free(d->bufstart);
	for (size_t i = 0; i < d->us.count; i++) {
		*d->us.array[i].ptr = NULL;
	}
	free(d->us.array);
	d->us.array = 0;
	d->us.cap = 0;
	d->us.count = 0;
}

void
dmove(/* create */ Document *dst, /* move */ Document *src)
{
	*dst = *src;
	for (size_t i = 0; i < dst->us.count; i++) {
		/* this is the point where you wish you wish you were programming in asm
		   so you didn't have to deal with c's stupid pointer arithmetic */
		/* cast everything to char *'s then cast back before assigning in case
		   we're working on a machine with unaliged struct members (can't think of an example though) */
		if ((char *)src <= (char *)dst->us.array[i].ptr && (char*)dst->us.array[i].ptr < (char *)src + sizeof(Document))
			dst->us.array[i].ptr = (char **)((char *)dst->us.array[i].ptr + ((char *)dst - (char *)src));
	}
}

bool
dreinit(Document *old, char *buf, size_t buflen, size_t contentlen)
{
	Document new;
	if (!dinit(&new, buf, buflen, contentlen)) {
		return false;
	}
	dfree(old);
	dmove(old, &new);
	return true;
}

/* index means index to character in the document. Ie. excluding the gap. */
char *
dindextopointer(const Document *d, size_t index)
{
	char *p = index <= (size_t)(d->curleft - d->bufstart) ?
		d->bufstart + index :
		d->bufstart + (index + (d->curright - d->curleft));
	assert_valid_pos(d, p);
	return p;
}

size_t
dpointertoindex(const Document *d, const char *p)
{
	assert_valid_pos(d, p);
	size_t index = p <= d->curleft ?
		p - d->bufstart :
		(p - d->bufstart) - (d->curright - d->curleft);
	return index;
}

size_t
dgetrangelength(const Document *d, const char *left, const char *right)
{
	if (right <= d->curleft || d->curright <= left) {
		return right - left;
	} else return right - left - (d->curright - d->curleft);
}

void
dgetrange(const Document *d, const char *left, const char *right, /*output */ char *buf)
{
	if (right <= d->curleft || d->curright <= left) {
		memcpy(buf, left, right - left);
	} else {
		memcpy(buf, left, d->curleft - left);
		memcpy(buf + (d->curleft - left), d->curright, right - d->curright);
	}
}

bool
drangeeq(const Document *d, const char *left, const char *right, const char *string, size_t len)
{
	bool r = (dgetrangelength(d, left, right) == len);
	if (r) {
		if (right <= d->curleft || d->curright <= left) {
			r = (0 == memcmp(left, string, len));
		} else {
			r = (0 == memcmp(left, string, d->curleft - left)) ||
				(0 == memcmp(d->curright, string + (d->curleft - left), right - d->curright)); 
		}
	}
	return r;
}

Action
actionreverse(Action a)
{
	switch(a.type) {
	case INSERT:
		a.type = DELETE;
		break;
	case DELETE:
		a.type = INSERT;
		break;
	default: fail();
	}
	return a;
}

void
actiondo(Action a, Document *d)
{
	switch (a.type) {
	case DELETE: {
		char *left = dindextopointer(d, a.position);
		char *right = dindextopointer(d, a.position + a.size);
		assert(drangeeq(d, left, right, a.data, a.size));
		ddeleterange(d, left, right);
	} break;
	case INSERT: {
		char *pos = dindextopointer(d, a.position);
		dinsert(d, pos, a.data, a.size);
	} break;
	default: fail();
	}
}

void
hrecord(History *h, Action a)
{
	h->a = grow(h->a, &h->max, h->cur+1, sizeof(*h->a));
	h->a[h->cur] = a;
	h->cur++;
	h->count = h->cur;
}

Action
hundo(History *h, Document *d)
{
	Action a = { .type = NOP };
	if (h->cur > 0) {
		h->cur--;
		a = actionreverse(h->a[h->cur]);
		actiondo(a, d);
	}
	return a;
}

Action
hredo(History *h, Document *d)
{
	Action a = { .type = NOP };
	if (h->cur < h->count) {
		a = h->a[h->cur];
		actiondo(a, d);
		h->cur++;
	}
	return a;
}

void
hinit(History *h, size_t capacity)
{
	h->a = umalloc(capacity * sizeof(Action));
	h->max = capacity;
	h->count = 0;
	h->cur = 0;
}

void
hfree(/* move */ History *h)
{
	free(h->a);
	h->a = 0;
	h->count = 0;
	h->max = 0;
	h->cur = 0;
}

void
edeleterange(char *left, char *right)
{
	Action a = {
		.type = DELETE,
		.position = dpointertoindex(&doc, left),
		.size = dgetrangelength(&doc, left, right),
	};
	a.data = malloc(a.size);
	dgetrange(&doc, left, right, a.data);
	hrecord(&history, a);
	actiondo(a, &doc);
}


void
einsert(char *position, char *data, size_t length)
{
	Action a = {
		.type = INSERT,
		.position = dpointertoindex(&doc, position),
		.data = malloc(length),
		.size = length,
	};
	memcpy(a.data, data, length);
	hrecord(&history, a);
	actiondo(a, &doc);
}


void
einsertchar(char *pos, Rune r)
{
	usadd(&doc.us, &pos, 0);
	char buf[UTF_SIZ];
	size_t len = utf8encode(r, buf);
	einsert(pos, buf, len);
	usremv(&doc.us, &pos);
}

void
edeletesel()
{
	assert_valid_pos(&doc, doc.selanchor);
	if (doc.selanchor <= doc.curleft)
		edeleterange(doc.selanchor, doc.curleft);
	else
		edeleterange(doc.curright, doc.selanchor);
	doc.selanchor = NULL;
}

char *
egetsel()
{
	if (!doc.selanchor) return NULL;
	else {
		return dgetsubstr(&doc, MIN(doc.curleft, doc.selanchor), MAX(doc.curleft, doc.selanchor));
	}
}

char *
egetline()
{
	return dgetsubstr(&doc, dwalkrow(&doc, doc.curleft, 0), dwalkrow(&doc, doc.curleft, +1));
}

void
ewrite(Rune r)
{
	if (doc.selanchor) edeletesel(&doc);
	einsertchar(doc.curleft, r);
}

void
ewritestr(uchar *str, size_t size)
{
	if (doc.selanchor) edeletesel(&doc);
	einsert(doc.curleft, (char *)str, size);
}

void
ejumptoline(long line)
{
	char *pos = dwalkrow(&doc, doc.bufstart, line-1);
	dnavigate(&doc, pos, false);
}

void
eupdatecursor(Action a)
{
	if (a.type != NOP)
		dnavigate(&doc, dindextopointer(&doc, a.position), false);
}

bool
ewritefile(const char *path)
{
	size_t leftlen = doc.curleft - doc.bufstart;
	size_t rightlen = doc.bufend - doc.curright;
	size_t len = leftlen + rightlen;
	char *buf = umalloc(len);
	if (!buf) {
		printsyserror("Could not write to file \"%s\", out of memory", path);
		return false;
	}
	memcpy(buf, doc.bufstart, leftlen);
	memcpy(buf+leftlen, doc.curright, rightlen);
	FILE *file = fopen(path, "w");
	if (!file) {
		printsyserror("Could not open file \"%s\" for writing", path);
		free(buf);
		return false;
	}
	if (!fwrite(buf, 1, len, file)) {
		printsyserror("Could open but not write to file \"%s\"", path);
		free(buf);
		fclose(file);
		return false;
	}
	free(buf);
	fclose(file);
	return true;
}

bool
ereadfromfile(const char *path)
{
	struct stat info;
	if (!fstatat(AT_FDCWD, path, &info, 0)) {
		printsyserror("Could not stat file \"%s\"", path);
		return false;
	}
	if (S_ISREG(info.st_mode)) {
		fprintf(stderr, "File \"%s\" is not a regular file.\n", path);
		return false;
	}
	FILE *file = fopen(path, "rw");
	if (!file) {
		printsyserror("Could not open file \"%s\" for read-write", path);
		return false;
	}
	if (-1 == fseek(file, 0, SEEK_END)) {
		printsyserror("Cannot seek file \"%s\"", path);
		fclose(file);
		return false;
	}
	size_t len = ftell(file);
	if (!~len) {
		printsyserror("Cannot get length of file \"%s\"", path);
		fclose(file);
		return false;
	}
	rewind(file);
	size_t alloclen = len+UTF_SIZ;
	char *buf = umalloc(alloclen);
	if (!buf) {
		printsyserror("Could not allocate new buffer to read file \"%s\"", path);
		fclose(file);
		return false;
	}
	if (len > fread(buf+(alloclen-len), 1, len, file)) {
		printsyserror("Could open, and get length of the file but could not read file \"%s\"", path);
		free(buf);
		fclose(file);
		return false;
	}
	fclose(file);
	if (!dreinit(&doc, buf, alloclen, len)) {
		fprintf(stderr, "Could open and read document but could not init the document\n");
		free(buf);
		return false;
	}
	return true;
}

void
edraw(Line *line, int colc, int rowc, int *curcol, int *currow)
{
	dscroll(&doc, rowc);
	const char *p = doc.renderstart;
	Glyph g;
	int r = 0, c = 0;
	bool insel = doc.selanchor && doc.selanchor < doc.renderstart;
	for (r = 0; r < rowc; r++) {
		memset(line[r], 0, colc * sizeof(Glyph));
	}
	r = 0;
	while (r < rowc) {
		if (0 == POSCMP(&doc, p, doc.selanchor)) insel ^= 1;
		if (0 == POSCMP(&doc, p, doc.curleft)) {
			*currow = r;
			*curcol = c;
			if (doc.selanchor) insel ^= 1;
		}
		g.u = dreadchar(&doc, p, &p, +1);
		g.fg = insel ? 0 : 1;
		g.bg = insel ? 1 : 0;
		g.mode = 0;
		if (g.u == RUNE_EOF) break;
		if (g.u == '\n') {
			g.u = ' ';
			line[r][c] = g;
			c = 0; r++;
		} else if (g.u == '\t') {
			g.u = ' ';
			do {
				line[r][c] = g;
				c++;
			} while (c < colc && (c & 7) != 0);
		} else {
			line[r][c] = g;
			c++;
		}
		if (c >= colc) {
			c = 0;
			r++;
			if (r >= rowc) break;
		}
	}
}

void
einit()
{
	/* if we can't initialise the document then it's probably best we give up entirely */
	char *buf = umalloc(10);
	if (!buf) {
		printsyserror("Could not initialize the document");
		exit(1);
	}
	if (!dinit(&doc, buf, 10, 0)) {
		exit(1);
	}
	hinit(&history, 16);
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
		if (arg->i > 0) einsertchar(p, '\t');
		else if (dreadchar(&doc, p, &dmy, +1) == '\t') edeleterange(p, p + 1);
	}
	usremv(&doc.us, &p);
	usremv(&doc.us, &selleft);
	usremv(&doc.us, &selright);
}

void
deletechar(const Arg *arg)
{
	if (doc.selanchor) {
		edeletesel();
	} else if (arg->i > 0) {
		edeleterange(doc.curright, dwalkrune(&doc, doc.curright, arg->i));
	} else if (arg->i < 0) {
		edeleterange(dwalkrune(&doc, doc.curleft, arg->i), doc.curleft);
	}
}

void
deleteword(const Arg *arg)
{
	if (doc.selanchor) {
		edeletesel();
	} else if (arg->i > 0) {
		edeleterange(doc.curright, dwalkword(&doc, doc.curright, arg->i));
	} else if (arg->i < 0) {
		edeleterange(dwalkword(&doc, doc.curleft, arg->i), doc.curleft);
	}
}

void
deleterow(const Arg *arg)
{
	(void)arg;
	if (doc.selanchor) {
		edeletesel();
	} else {
		edeleterange(dwalkrow(&doc, doc.curleft, 0), dwalkrow(&doc, doc.curleft, +1));
	}
}

void
selectdocument(const Arg *dummy)
{
	(void)dummy;
	doc.selanchor = doc.bufstart;
	dnavigate(&doc, doc.bufend, true);
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
		if (0 == POSCMP(&doc, pos, doc.bufstart) && arg->i < 0) break;
		if (0 == POSCMP(&doc, pos, doc.bufend) && arg->i > 0) break;
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
	einsertchar(doc.curleft, '\n');
}

void
save(const Arg *arg)
{
	(void)arg;
	/* error is outputted by the function so we're covered */
	ewritefile(filename);
}

void
load(const Arg *arg)
{
	(void)arg;
	ereadfromfile(filename);
}

void
undo(const Arg *dummy)
{
	(void)dummy;
	Action a = hundo(&history, &doc);
	eupdatecursor(a);
}

void
redo(const Arg *dummy)
{
	(void)dummy;
	Action a = hredo(&history, &doc);
	eupdatecursor(a);
}
