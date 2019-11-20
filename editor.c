/* See LICENSE for license details. */
#include <string.h>

#include "editor.h"

#define MAX_LINE_LEN 1024

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
} DocumentCursor;

/* Globals */
static Document document;
static DocumentCursor curosr;

int
dcountbackline(const Document *d, int line)
{
	if (line < 0) {
		int count = -line;
		char *p = d->buf + d->startchar;
		for (int i = 0; i < count; i++) {
			p = memrchr(d->buf, '\n', p - d->buf);
			if (!p && i == count-1) {
				return 0;
			} else if (!p) {
				return -1;
			}
		}
		return p - d->buf;
	} else return d->startchar;
}

void
dcountforwardline(const Document *d, int line)
{
	if (line >= d->linect) {
		int count = line - d->linect+1;
		char *p = d->buf + d->startchar;
		for (int i = 0; i < count; i++) {
			p = memchr(p, '\n', d->buf + d->buflen - p);
			if (!p && i == count-1) {
				return d->buflen;
			} else if (!p) {
				return -1;
			}
		}
		return p - d->buf;
	} else return d->endchar;
}

void
dgrowdown(Document *d, int start)
{

}

void
ddeletelines(Document *d, int start, int end)
{
	if (start < 0) {
		int startchar = dcountbackline(d, start);
		if (!~startchar) startchar = 0;
		int diff = d->startchar - startchar;
		d->bytelen -= diff;
		d->startchar = startchar;
		start = 0;
	}
	if (end >= d->linect) {
		int endchar = dcountforwardline(d, end);
		if (!~endchar) endchar = d->buflen;
		int diff = endchar - d->endchar;
		d->bytelen -= diff;
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
		ddeletelines(line1+1, line2-1);
		line2 = line1 + 1;
	}
	if (line1 < 0) {
		int startchar = dcountbackline(d, line1);
		if (~startchar) startchar += pos1;
		int diff = d->startchar - startchar;
		d->bytelen -= diff;
		d->startchar = startchar;
		line1 = 0;
		pos1 = 0;
	}
	if (line2 >= d->linect) {
		int endchar = dcountforwardline(d, line2);
		if (~endchar) endchar += pos2;
		int diff = endchar - d->endchar;
		d->bytelen -= diff;
		d->endchar = endchart;
		line2 = d->linect;
		pos2 = 0;
	}
	if (line1 == line2 && line1 < linect) {
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
		ddeletelines(Document *d, line1+1, line2);
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
		memmove(line->data(line->data, line->data + arg->i, line.bytelen);
		for (int i = 0; i < arg->i; i++) {
			line.data[i] = '\t';
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
		deleterange(&document, cursor.line, 0, cursor.line, -arg->i);
	}
}

void
deletechar(const Arg *arg)
{
	DocumentLine* line = &document.line[cursor.line];
	int min = MIN(cursor.pos, cursor.pos + arg->i);
	int max = MAX(cursor.pos, cursor.pos + arg->i);
	if (curosr.max >= line->bytelen) {
		mergelines(cursor.line, cursor.line+1);
	} else {
		deletelinerange(min, max);
	}
}
