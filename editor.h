/* See LICENSE for license details. */

#include "cdoedit.h"

void einit();
char *egetsel();
char *egetline();
void ewrite(Rune r);
void ewritestr(uchar *str, size_t size);
void edraw(Line *line, int colc, int rowc, int *curcol, int *currow);
void ejumptoline(long line);
void changeindent(const Arg *);
void deletechar(const Arg *);
void deleteword(const Arg *);
void deleterow(const Arg *);
void selectdocument(const Arg *);
void navchar(const Arg *);
void navdocument(const Arg *);
void navline(const Arg *);
void navpage(const Arg *);
void navparagraph(const Arg *);
void navrow(const Arg *);
void navword(const Arg *);
void newline(const Arg *);
void new(const Arg *);
void load(const Arg *);
void save(const Arg *);
void saveas(const Arg *);
