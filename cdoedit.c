/* See LICENSE for license details. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "win.h"

/* Internal representation of the screen */
typedef struct {
	int row;      /* nb row */
	int col;      /* nb col */
	Line *line;   /* screen */
} Term;

/* Globals */
static Term term;

void
tnew(int col, int row)
{
	term = (Term){ 0 };
	tresize(col, row);
}
void
tresize(int col, int row)
{
	int i;

	if (col < 1 || row < 1) {
		fprintf(stderr,
		        "tresize: error resizing to %dx%d\n", col, row);
		return;
	}

	for (i = row; i < term.row; i++) {
		free(term.line[i]);
	}

	/* resize to new height */
	term.line = urealloc(term.line, row * sizeof(Line));

	/* resize each row to new width, zero-pad if needed */
	for (i = 0; i < MIN(row, term.row); i++) {
		term.line[i] = urealloc(term.line[i], col * sizeof(Glyph));
	}

	/* allocate any new rows */
	for (; i < row; i++) {
		term.line[i] = umalloc(col * sizeof(Glyph));
	}
	/* update display buffer size */
	term.col = col;
	term.row = row;
}

void
resettitle(void)
{
	xsettitle(NULL);
}

void
redraw(void)
{
	int cx, cy;
	edraw(term.line, term.col, term.row, &cx, &cy);
	
	if (!xstartdraw())
		return;

	/* adjust cursor position */
	LIMIT(cx, 0, term.col-1);
	LIMIT(cy, 0, term.row-1);

	int y;
	for (y = 0; y < term.row; y++) {
		xdrawline(term.line[y], 0, y, term.col);
	}
	xdrawcursor(cx, cy, term.line[cy][cx]);
	xfinishdraw();
	xximspot(cx, cy);
}
