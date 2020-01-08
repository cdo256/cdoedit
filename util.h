/* See LICENSE for license details. */

#include <stdint.h>
#include <sys/types.h>
#include <wchar.h>

/* macros */
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define TIMEDIFF(t1, t2)	((t1.tv_sec-t2.tv_sec)*1000 + \
				(t1.tv_nsec-t2.tv_nsec)/1E6)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))
#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))
#define SIGN(a)                  ((a)>0 ? +1 : (a)<0 ? -1 : 0)
#define STUPIDLY_BIG(x)          ((size_t)(x) & 0xFFFF000000000000ULL)

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

typedef uint_least32_t Rune;

#define RUNE_EOF ((Rune)(~0ULL))

void udie(const char *, ...);
size_t utf8encode(Rune, char *);

void *umalloc(size_t);
void *urealloc(void *, size_t);
char *ustrdup(char *);

#define UTF_SIZ 4
#define UTF_INVALID 0xFFFD
#define ERROR_BUF_LEN 8192

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
#define fail()
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

size_t memctchr(const char *s, int c, size_t n);
void *grow(void *buf, size_t *len, size_t newlen, size_t entrysize);
void userwarning(const char *s, ...);
void printsyserror(const char *str, ...);
size_t utf8validate(Rune *u, size_t i);
Rune utf8decodebyte(char c, size_t *i);
char utf8encodebyte(Rune u, size_t i);
size_t utf8decode(const char *c, Rune *u, size_t clen);
size_t utf8encode(Rune u, char *c);
