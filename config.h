/* See LICENSE file for copyright and license details. */

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static char *font = "Liberation Mono:pixelsize=12:antialias=true:autohint=true";
static int borderpx = 2;

/* identification sequence returned in DA and DECID */
char *vtiden = "\033[?6c";

/* Kerning / character bounding-box multipliers */
static float cwscale = 1.0;
static float chscale = 1.0;

/*
 * word delimiter string
 *
 * More advanced example: L" `'\"()[]{}"
 */
wchar_t *worddelimiters = L" ";

/* alt screens */
int allowaltscreen = 1;

/* frames per second cdoedit should at maximum draw to the screen */
static unsigned int xfps = 120;
static unsigned int actionfps = 30;

/*
 * blinking timeout (set to 0 to disable blinking) for the terminal blinking
 * attribute.
 */
static unsigned int blinktimeout = 800;

/*
 * thickness of underline and bar cursors
 */
static unsigned int cursorthickness = 1;

/*
 * bell volume. It must be a value between -100 and 100. Use 0 for disabling
 * it
 */
static int bellvolume = 0;

/*
 * spaces per tab
 */
unsigned int tabspaces = 8;

/*
 * Default colors (colorname index)
 * foreground, background, cursor, reverse cursor
 */
unsigned int defaultfg = 1;
unsigned int defaultbg = 0;
static unsigned int defaultcs = 1;
static unsigned int defaultrcs = 0;

/*
 * Default shape of cursor
 * 2: Block ("█")
 * 4: Underline ("_")
 * 6: Bar ("|")
 * 7: Snowman ("☃")
 */
static unsigned int cursorshape = 6;

/*
 * Default columns and rows numbers
 */

static unsigned int cols = 80;
static unsigned int rows = 24;

/*
 * Default colour and shape of the mouse cursor
 */
static unsigned int mouseshape = XC_xterm;

/*
 * Color used to display font attributes when fontconfig selected a font which
 * doesn't match the ones requested.
 */
static unsigned int defaultattr = 1;

/* Internal keyboard shortcuts. */
#define META Mod1Mask
#define CTRL ControlMask
#define SHIFT ShiftMask
#define NOSHIFT 0
#define DEFAULT_MASK (~(Mod2Mask|XK_SWITCH_MOD|LockMask))
#define IGNORE_SHIFT (DEFAULT_MASK&~SHIFT)

static Shortcut shortcuts[] = {
	/* editing */
	/* modmask          modval                keysym          function        argument */
	{ DEFAULT_MASK,     0,                    XK_Tab,         changeindent,   {.i = +1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Tab,         changeindent,   {.i = -1} },
	{ IGNORE_SHIFT,     CTRL,                 XK_Home,        zoomreset,      {.f =  0} },
	{ IGNORE_SHIFT,     CTRL,                 XK_Num_Lock,    numlock,        {.i =  0} },
	{ IGNORE_SHIFT,     0,                    XK_Return,      newline,        {.i =  0} },
	{ IGNORE_SHIFT,     0,                    XK_BackSpace,   deletechar,     {.i = -1} },
	{ IGNORE_SHIFT,     0,                    XK_Delete,      deletechar,     {.i = +1} },
	{ IGNORE_SHIFT,     CTRL,                 XK_BackSpace,   deleteword,     {.i = -1} },
	{ IGNORE_SHIFT,     CTRL,                 XK_Delete,      deleteword,     {.i = +1} },
	{ IGNORE_SHIFT,     CTRL,                 'K',            clipcutrow,     {.i =  0} },
	{ IGNORE_SHIFT,     CTRL,                 'k',            clipcutrow,     {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'C',            clipcopy,       {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'c',            clipcopy,       {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'V',            clippaste,      {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'v',            clippaste,      {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'X',            clipcut,        {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'x',            clipcut,        {.i =  0} },

	/* navigation */
	/* modmask          modval                keysym          function        argument */
	{ DEFAULT_MASK,     0,                    XK_Home,        navline,        {.i = -1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Home,        navline,        {.i = -2} },
	{ DEFAULT_MASK,     0,                    XK_End,         navline,        {.i = +1} },
	{ DEFAULT_MASK,     SHIFT,                XK_End,         navline,        {.i = +2} },
	{ DEFAULT_MASK,     CTRL,                 XK_Home,        navdocument,    {.i = -1} },
	{ DEFAULT_MASK,     CTRL|SHIFT,           XK_Home,        navdocument,    {.i = -2} },
	{ DEFAULT_MASK,     CTRL,                 XK_End,         navdocument,    {.i = +1} },
	{ DEFAULT_MASK,     CTRL|SHIFT,           XK_End,         navdocument,    {.i = +2} },
	{ DEFAULT_MASK,     0,                    XK_Up,          navrow,         {.i = -1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Up,          navrow,         {.i = -2} },
	{ DEFAULT_MASK,     0,                    XK_Down,        navrow,         {.i = +1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Down,        navrow,         {.i = +2} },
	{ DEFAULT_MASK,     0,                    XK_Left,        navchar,        {.i = -1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Left,        navchar,        {.i = -2} },
	{ DEFAULT_MASK,     0,                    XK_Right,       navchar,        {.i = +1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Right,       navchar,        {.i = +2} },
	{ DEFAULT_MASK,     CTRL,                 XK_Up,          navparagraph,   {.i = -1} },
	{ DEFAULT_MASK,     CTRL|SHIFT,           XK_Up,          navparagraph,   {.i = -2} },
	{ DEFAULT_MASK,     CTRL,                 XK_Down,        navparagraph,   {.i = +1} },
	{ DEFAULT_MASK,     CTRL|SHIFT,           XK_Down,        navparagraph,   {.i = +2} },
	{ DEFAULT_MASK,     CTRL,                 XK_Left,        navword,        {.i = -1} },
	{ DEFAULT_MASK,     CTRL|SHIFT,           XK_Left,        navword,        {.i = -2} },
	{ DEFAULT_MASK,     CTRL,                 XK_Right,       navword,        {.i = +1} },
	{ DEFAULT_MASK,     CTRL|SHIFT,           XK_Right,       navword,        {.i = +2} },
	{ DEFAULT_MASK,     0,                    XK_Page_Up,     navpage,        {.i = -1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Page_Up,     navpage,        {.i = -2} },
	{ DEFAULT_MASK,     0,                    XK_Page_Down,   navpage,        {.i = +1} },
	{ DEFAULT_MASK,     SHIFT,                XK_Page_Down,   navpage,        {.i = +2} },

	/* other */
	/* modmask          modval                keysym          function        argument */
	{ IGNORE_SHIFT,     CTRL,                 XK_plus,        zoom,           {.f = +1} },
	{ IGNORE_SHIFT,     CTRL,                 XK_minus,       zoom,           {.f = -1} },
	{ IGNORE_SHIFT,     CTRL,                 XK_equal,       zoom,           {.f = +1} },
	{ DEFAULT_MASK,     CTRL,                 'S',            save,           {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 's',            save,           {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'R',            load,           {.i =  0} },
	{ DEFAULT_MASK,     CTRL,                 'r',            load,           {.i =  0} },
};

/*
 * Printable characters in ASCII, used to estimate the advance width
 * of single wide characters.
 */
static char ascii_printable[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";

