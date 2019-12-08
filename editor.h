/* See LICENSE for license details. */

#include "cdoedit.h"

void einit();
void ewrite(Rune r);
void edraw(Line *line, int colc, int rowc, int *curcol, int *currow);

void changeindent(const Arg *);
void deletechar(const Arg *);
void deleteword(const Arg *);
void deleterow(const Arg *);
void navchar(const Arg *);
void navdocument(const Arg *);
void navline(const Arg *);
void navpage(const Arg *);
void navparagraph(const Arg *);
void navrow(const Arg *);
void navword(const Arg *);
void newline(const Arg *);
